#include "calculate_bucket.hpp"

F1Calculator::F1Calculator(uint8_t k, const uint8_t* orig_key)
{
    uint8_t enc_key[32];
    size_t buf_blocks = cdiv(k << kBatchSizes, kF1BlockSizeBits) + 1;
    this->k_ = k;
    this->buf_ = new uint8_t[buf_blocks * kF1BlockSizeBits / 8 + 7];

    // First byte is 1, the index of this table
    enc_key[0] = 1;
    memcpy(enc_key + 1, orig_key, 31);

    // Setup ChaCha8 context with zero-filled IV
    chacha8_keysetup(&this->enc_ctx_, enc_key, 256, NULL);
}

Bits F1Calculator::CalculateF(const Bits& L) const
{
    uint16_t num_output_bits = k_;
    uint16_t block_size_bits = kF1BlockSizeBits;

    // Calculates the counter that will be used to get ChaCha8 keystream.
    // Since k < block_size_bits, we can fit several k bit blocks into one
    // ChaCha8 block.
    uint128_t counter_bit = L.GetValue() * (uint128_t)num_output_bits;
    uint64_t counter = counter_bit / block_size_bits;

    // How many bits are before L, in the current block
    uint32_t bits_before_L = counter_bit % block_size_bits;

    // How many bits of L are in the current block (the rest are in the next block)
    const uint16_t bits_of_L =
        std::min((uint16_t)(block_size_bits - bits_before_L), num_output_bits);

    // True if L is divided into two blocks, and therefore 2 ChaCha8
    // keystream blocks will be generated.
    const bool spans_two_blocks = bits_of_L < num_output_bits;

    uint8_t ciphertext_bytes[kF1BlockSizeBits / 8];
    Bits output_bits;

    // This counter is used to initialize words 12 and 13 of ChaCha8
    // initial state (4x4 matrix of 32-bit words). This is similar to
    // encrypting plaintext at a given offset, but we have no
    // plaintext, so no XORing at the end.
    chacha8_get_keystream(&this->enc_ctx_, counter, 1, ciphertext_bytes);
    Bits ciphertext0(ciphertext_bytes, block_size_bits / 8, block_size_bits);

    if (spans_two_blocks) {
        // Performs another encryption if necessary
        ++counter;
        chacha8_get_keystream(&this->enc_ctx_, counter, 1, ciphertext_bytes);
        Bits ciphertext1(ciphertext_bytes, block_size_bits / 8, block_size_bits);
        output_bits =
            ciphertext0.Slice(bits_before_L) + ciphertext1.Slice(0, num_output_bits - bits_of_L);
    } else {
        output_bits = ciphertext0.Slice(bits_before_L, bits_before_L + num_output_bits);
    }

    // Adds the first few bits of L to the end of the output, production k + kExtraBits of
    // output
    Bits extra_data = L.Slice(0, kExtraBits);
    if (extra_data.GetSize() < kExtraBits) {
        extra_data += Bits(0, kExtraBits - extra_data.GetSize());
    }
    return output_bits + extra_data;
}

void F1Calculator::CalculateBuckets(uint64_t first_x, uint64_t n, uint64_t* res)
{
    uint64_t start = first_x * k_ / kF1BlockSizeBits;
    // 'end' is one past the last keystream block number to be generated
    uint64_t end = cdiv((first_x + n) * k_, kF1BlockSizeBits);
    uint64_t num_blocks = end - start;
    uint32_t start_bit = first_x * k_ % kF1BlockSizeBits;
    uint8_t x_shift = k_ - kExtraBits;

    assert(n <= (1U << kBatchSizes));

    chacha8_get_keystream(&this->enc_ctx_, start, num_blocks, buf_);
    for (uint64_t x = first_x; x < first_x + n; x++) {
        uint64_t y = Util::SliceInt64FromBytes(buf_, start_bit, k_);

        res[x - first_x] = (y << kExtraBits) | (x >> x_shift);

        start_bit += k_;
    }
}

F1Calculator::~F1Calculator() { delete[] buf_; }

FxCalculator::FxCalculator(uint8_t k, uint8_t table_index)
{
    this->k_ = k;
    this->table_index_ = table_index;

    this->rmap.resize(kBC);
    if (!initialized) {
        initialized = true;
        load_tables();
    }
}

std::pair<Bits, Bits> FxCalculator::CalculateBucket(const Bits& y1, const Bits& L, const Bits& R)
    const
{
    Bits input;
    uint8_t input_bytes[64];
    uint8_t hash_bytes[32];
    blake3_hasher hasher;
    uint64_t f;
    Bits c;

    if (table_index_ < 4) {
        c = L + R;
        input = y1 + c;
    } else {
        input = y1 + L + R;
    }

    input.ToBytes(input_bytes);

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input_bytes, cdiv(input.GetSize(), 8));
    blake3_hasher_finalize(&hasher, hash_bytes, sizeof(hash_bytes));

    f = Util::EightBytesToInt(hash_bytes) >> (64 - (k_ + kExtraBits));

    if (table_index_ < 4) {
        // c is already computed
    } else if (table_index_ < 7) {
        uint8_t len = kVectorLens[table_index_ + 1];
        uint8_t start_byte = (k_ + kExtraBits) / 8;
        uint8_t end_bit = k_ + kExtraBits + k_ * len;
        uint8_t end_byte = cdiv(end_bit, 8);

        // TODO: proper support for partial bytes in Bits ctor
        c = Bits(hash_bytes + start_byte, end_byte - start_byte, (end_byte - start_byte) * 8);

        c = c.Slice((k_ + kExtraBits) % 8, end_bit - start_byte * 8);
    }

    return std::make_pair(Bits(f, k_ + kExtraBits), c);
}

int32_t FxCalculator::FindMatches(
    const std::vector<PlotEntry>& bucket_L,
    const std::vector<PlotEntry>& bucket_R,
    uint16_t* idx_L,
    uint16_t* idx_R)
{
    int32_t idx_count = 0;
    uint16_t parity = (bucket_L[0].y / kBC) % 2;

    for (size_t yl : rmap_clean) {
        this->rmap[yl].count = 0;
    }
    rmap_clean.clear();

    uint64_t remove = (bucket_R[0].y / kBC) * kBC;
    for (size_t pos_R = 0; pos_R < bucket_R.size(); pos_R++) {
        uint64_t r_y = bucket_R[pos_R].y - remove;

        if (!rmap[r_y].count) {
            rmap[r_y].pos = pos_R;
        }
        rmap[r_y].count++;
        rmap_clean.push_back(r_y);
    }

    const uint64_t remove_y = remove - kBC;
    for (size_t pos_L = 0; pos_L < bucket_L.size(); pos_L++) {
        const uint64_t r = bucket_L[pos_L].y - remove_y;
        for (uint8_t i = 0; i < kExtraBitsPow; i++) {
            const uint16_t r_target = L_targets[parity][r][i];
            if (idx_L != nullptr) {
                for (size_t j = 0; j < rmap[r_target].count; j++) {
                    idx_L[idx_count] = pos_L;
                    idx_R[idx_count] = rmap[r_target].pos + j;
                    idx_count++;
                }
            } else {
                idx_count += rmap[r_target].count;
            }
        }
    }
    return idx_count;
}

void FxCalculator::load_tables()
{
    for (uint8_t parity = 0; parity < 2; parity++) {
        for (uint16_t i = 0; i < kBC; i++) {
            uint16_t indJ = i / kC;
            for (uint16_t m = 0; m < kExtraBitsPow; m++) {
                uint16_t yr =
                    ((indJ + m) % kB) * kC + (((2 * m + parity) * (2 * m + parity) + i) % kC);
                L_targets[parity][i][m] = yr;
            }
        }
    }
}
