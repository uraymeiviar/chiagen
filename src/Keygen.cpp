#include "Keygen.hpp"
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <sstream>
#include <string>
#include <intrin.h>

#if FP_PRIME < 1536

#define RLC_G1_LOWER			ep_
#define RLC_G1_UPPER			EP

#if FP_PRIME == 509
#define RLC_G2_LOWER			ep4_
#else
#define RLC_G2_LOWER			ep2_
#endif

#define RLC_G2_UPPER			EP

#if FP_PRIME == 509
#define RLC_GT_LOWER			fp24_
#else
#define RLC_GT_LOWER			fp12_
#endif

#define RLC_PC_LOWER			pp_

#else
#define RLC_G1_LOWER			ep_
#define RLC_G1_UPPER			EP
#define RLC_G2_LOWER			ep_
#define RLC_G2_UPPER			EP
#define RLC_GT_LOWER			fp2_
#define RLC_PC_LOWER			pp_
#endif

typedef RLC_CAT(RLC_G1_LOWER, t) g1_t;
#define g1_mul_dig(R, P, K)		RLC_CAT(RLC_G1_LOWER, mul_dig)(R, P, K)
#define g1_neg(R, P)		RLC_CAT(RLC_G1_LOWER, neg)(R, P)
#define pc_get_ord(N)		RLC_CAT(RLC_G1_LOWER, curve_get_ord)(N)
#define g1_is_infty(P)		RLC_CAT(RLC_G1_LOWER, is_infty)(P)
#define g1_null(A)			RLC_CAT(RLC_G1_LOWER, null)(A)
#define g1_new(A)			RLC_CAT(RLC_G1_LOWER, new)(A)
#define g1_on_curve(P)		RLC_CAT(RLC_G1_LOWER, on_curve)(P)
#define g1_cmp(P, Q)		RLC_CAT(RLC_G1_LOWER, cmp)(P, Q)
#define g1_free(A)			RLC_CAT(RLC_G1_LOWER, free)(A)

void g1_mul(g1_t c, g1_t a, bn_t b) {
	bn_t n, _b;

	bn_null(n);
	bn_null(_b);

	if (bn_bits(b) <= RLC_DIG) {
		g1_mul_dig(c, a, b->dp[0]);
		if (bn_sign(b) == RLC_NEG) {
			g1_neg(c, c);
		}
		return;
	}

	RLC_TRY {
		bn_new(n);
		bn_new(_b);

		pc_get_ord(n);
		bn_mod(_b, b, n);

		RLC_CAT(RLC_G1_LOWER, mul)(c, a, _b);
	} RLC_CATCH_ANY {
		RLC_THROW(ERR_CAUGHT);
	} RLC_FINALLY {
		bn_free(n);
		bn_free(_b);
	}
}

void g1_mul_gen(g1_t c, bn_t b) {
	bn_t n, _b;

	bn_null(n);
	bn_null(_b);

	RLC_TRY {
		bn_new(n);
		bn_new(_b);

		pc_get_ord(n);
		bn_mod(_b, b, n);

		RLC_CAT(RLC_G1_LOWER, mul_gen)(c, _b);
	} RLC_CATCH_ANY {
		RLC_THROW(ERR_CAUGHT);
	} RLC_FINALLY {
		bn_free(n);
		bn_free(_b);
	}
}

int g1_is_valid(g1_t a) {
	bn_t n;
	g1_t t, u, v;
	int r = 0;

	if (g1_is_infty(a)) {
		return 0;
	}

	bn_null(n);
	g1_null(t);
	g1_null(u);
	g1_null(v);

	RLC_TRY {
		bn_new(n);
		g1_new(t);
		g1_new(u);
		g1_new(v);

		ep_curve_get_cof(n);
		if (bn_cmp_dig(n, 1) == RLC_EQ) {
			/* If curve has prime order, simpler to check if point on curve. */
			r = g1_on_curve(a);
		} else {
			switch (ep_curve_is_pairf()) {
				/* Formulas from "Faster Subgroup Checks for BLS12-381" by Bowe.
				 * https://eprint.iacr.org/2019/814.pdf */
				case EP_B12:
					/* Check [(z^2-1)](2\psi(P)-P-\psi^2(P)) == [3]\psi^2(P).
					 * Since \psi(P) = [\lambda]P = [z^2 - 1]P, it is the same
					 * as checking \psi(2\psi(P)-P-\psi^2(P)) == [3]\psi^2(P),
					 * or \psi((\psi-1)^2(P)) == [-3]*\psi^2(P). */
					ep_psi(v, a);
					ep_sub(t, v, a);
					ep_psi(u, v);
					ep_psi(v, t);
					ep_sub(v, v, t);
					ep_psi(t, v);
					ep_dbl(v, u);
					ep_add(u, u, v);
					ep_neg(u, u);
					r = ep_on_curve(t) && (ep_cmp(t, u) == RLC_EQ);
					break;
				default:
					pc_get_ord(n);
					bn_sub_dig(n, n, 1);
					/* Otherwise, check order explicitly. */
					/* We use fast scalar multiplication methods here, because
					 * they should work only in the correct order. */
					g1_mul(u, a, n);
					g1_neg(u, u);
					r = g1_on_curve(a) && (g1_cmp(u, a) == RLC_EQ);
					break;
			}
		}
	} RLC_CATCH_ANY {
		RLC_THROW(ERR_CAUGHT);
	} RLC_FINALLY {
		bn_free(n);
		g1_free(t);
		g1_free(u);
		g1_free(v);
	}

	return r;
}

bool Init()
{
	core_init();
    if (err_get_code() != RLC_OK) {
        throw std::runtime_error("core_init() failed");
    }

    const int r = ep_param_set_any_pairf();
    if (r != RLC_OK) {
        throw std::runtime_error("ep_param_set_any_pairf() failed");
    }

	return r == RLC_OK;
}

void CheckRelicErrors()
{
    if (!core_get()) {
        throw std::runtime_error("Library not initialized properly. Call BLS::Init()");
    }
    if (core_get()->code != RLC_OK) {
        core_get()->code = RLC_OK;
        throw std::invalid_argument("Relic library error");
    }
}

//==================================================================================================

template<class T> static T* AllocT(size_t numTs) {
	return static_cast<T*>(malloc(sizeof(T) * numTs));
}

static std::string HexStr(const uint8_t* data, size_t len) {
    std::stringstream s;
    s << std::hex;
    for (int i=0; i < len; ++i)
        s << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return s.str();
}

static std::string HexStr(const std::vector<uint8_t> &data) {
    std::stringstream s;
    s << std::hex;
    for (int i=0; i < data.size(); ++i)
        s << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return s.str();
}

static uint32_t FourBytesToInt(const uint8_t* bytes) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 4; i++) {
        uint32_t addend = bytes[i] << (8 * (3 - i));
        sum += addend;
    }
    return sum;
}

static void IntToFourBytes(uint8_t* result,
                            const uint32_t input) {
    for (size_t i = 0; i < 4; i++) {
        result[3 - i] = (input >> (i * 8));
    }
}

void Hash256(uint8_t* output, const uint8_t* message, size_t messageLen) {
    md_map_sh256(output, message, messageLen);
}

G1Element G1FromNative(const ep_t element)
{
	G1Element ele;
	ep_copy(ele.p, element);
	ele.CheckValid();
	return ele;
}

void Extract(uint8_t* prk_output, const uint8_t* salt, const size_t saltLen, const uint8_t* ikm, const size_t ikm_len) {
	// assert(saltLen == 4);  // Used for EIP2333 key derivation
	// assert(ikm_len == 32);  // Used for EIP2333 key derivation
	// Hash256 used as the hash function (sha256)
	// PRK Output is 32 bytes (HashLen)
	md_hmac(prk_output, ikm, ikm_len, salt, saltLen);
}

void Expand(uint8_t* okm, size_t L, const uint8_t* prk, const uint8_t* info, const size_t infoLen) {
	assert(L <= 255 * HASH_LEN); // L <= 255 * HashLen
	assert(infoLen >= 0);
	size_t N = (L + HASH_LEN - 1) / HASH_LEN; // Round up
	size_t bytesWritten = 0;

	uint8_t* T = AllocT<uint8_t>(HASH_LEN);
	uint8_t* hmacInput1 = AllocT<uint8_t>(infoLen + 1);
	uint8_t* hmacInput = AllocT<uint8_t>(HASH_LEN + infoLen + 1);

	assert(N >= 1 && N <= 255);

	for (size_t i = 1; i <= N; i++) {
		if (i == 1) {
			memcpy(hmacInput1, info, infoLen);
			hmacInput1[infoLen] = i;
			md_hmac(T, hmacInput1, infoLen + 1, prk, HASH_LEN);
		} else {
			memcpy(hmacInput, T, HASH_LEN);
			memcpy(hmacInput + HASH_LEN, info, infoLen);
			hmacInput[HASH_LEN + infoLen] = i;
			md_hmac(T, hmacInput, HASH_LEN + infoLen + 1, prk, HASH_LEN);
		}
		size_t to_write = L - bytesWritten;
		if (to_write > HASH_LEN) {
			to_write = HASH_LEN;
		}
		assert (to_write > 0 && to_write <= HASH_LEN);
		memcpy(okm + bytesWritten, T, to_write);
		bytesWritten += to_write;
	}
	free(T);
	free(hmacInput1);
	free(hmacInput);
	assert(bytesWritten == L);
}

void ExtractExpand(uint8_t* output, size_t outputLen,
							  const uint8_t* key, size_t keyLen,
							  const uint8_t* salt, size_t saltLen,
							  const uint8_t* info, size_t infoLen) {
		
	uint8_t* prk = AllocT<uint8_t>(HASH_LEN);
	Extract(prk, salt, saltLen, key, keyLen);
	Expand(output, outputLen, prk, info, infoLen);
	free(prk);
}

PrivateKey KeyGen(const Bytes& seed)
{
	// KeyGen
	// 1. PRK = HKDF-Extract("BLS-SIG-KEYGEN-SALT-", IKM || I2OSP(0, 1))
	// 2. OKM = HKDF-Expand(PRK, keyInfo || I2OSP(L, 2), L)
	// 3. SK = OS2IP(OKM) mod r
	// 4. return SK

	const uint8_t info[1] = {0};
	const size_t infoLen = 0;

	// Required by the ietf spec to be at least 32 bytes
	if (seed.size() < 32) {
		throw std::invalid_argument("Seed size must be at least 32 bytes");
	}

	// "BLS-SIG-KEYGEN-SALT-" in ascii
	const uint8_t saltHkdf[20] = {66, 76, 83, 45, 83, 73, 71, 45, 75, 69,
								89, 71, 69, 78, 45, 83, 65, 76, 84, 45};

	uint8_t *prk = AllocT<uint8_t>(32);
	uint8_t *ikmHkdf = AllocT<uint8_t>(seed.size() + 1);
	memcpy(ikmHkdf, seed.begin(), seed.size());
	ikmHkdf[seed.size()] = 0;

	const uint8_t L = 48;  // `ceil((3 * ceil(log2(r))) / 16)`, where `r` is the
						// order of the BLS 12-381 curve

	uint8_t *okmHkdf = AllocT<uint8_t>(L);

	uint8_t keyInfoHkdf[infoLen + 2];
	memcpy(keyInfoHkdf, info, infoLen);
	keyInfoHkdf[infoLen] = 0;  // Two bytes for L, 0 and 48
	keyInfoHkdf[infoLen + 1] = L;

	ExtractExpand(
		okmHkdf,
		L,
		ikmHkdf,
		seed.size() + 1,
		saltHkdf,
		20,
		keyInfoHkdf,
		infoLen + 2);

	bn_t order;
	bn_make(order, RLC_BN_SIZE);
	bn_copy(order, &core_get()->ep_r);

    // Make sure private key is less than the curve order
    bn_t *skBn = AllocT<bn_t>(1);
    bn_new(*skBn);
    bn_read_bin(*skBn, okmHkdf, L);
    bn_mod_basic(*skBn, *skBn, order);

    uint8_t *skBytes = AllocT<uint8_t>(32);
    bn_write_bin(skBytes, 32, *skBn);
    PrivateKey k = PrivateKey::FromBytes(Bytes(skBytes, 32));

	free(prk);
	free(ikmHkdf);
	free(skBn);
	free(okmHkdf);
	free(skBytes);

	return k;
}

void IKMToLamportSk(uint8_t* outputLamportSk, const uint8_t* ikm, size_t ikmLen, const uint8_t* salt, size_t saltLen)  {
	// Expands the ikm to 255*HASH_LEN bytes for the lamport sk
	const uint8_t info[1] = {0};
	ExtractExpand(outputLamportSk, HASH_LEN * 255, ikm, ikmLen, salt, saltLen, info, 0);
}

void ParentSkToLamportPK(uint8_t* outputLamportPk, const PrivateKey& parentSk, uint32_t index) {
	uint8_t* salt = AllocT<uint8_t>(4);
	uint8_t* ikm = AllocT<uint8_t>(HASH_LEN);
	uint8_t* notIkm = AllocT<uint8_t>(HASH_LEN);
	uint8_t* lamport0 = AllocT<uint8_t>(HASH_LEN * 255);
	uint8_t* lamport1 = AllocT<uint8_t>(HASH_LEN * 255);

	IntToFourBytes(salt, index);
	parentSk.Serialize(ikm);

	for (size_t i = 0; i < HASH_LEN; i++) {  // Flips the bits
		notIkm[i] = ikm[i] ^ 0xff;
	}

	IKMToLamportSk(lamport0, ikm, HASH_LEN, salt, 4);
	IKMToLamportSk(lamport1, notIkm, HASH_LEN, salt, 4);

	uint8_t* lamportPk = AllocT<uint8_t>(HASH_LEN * 255 * 2);

	for (size_t i = 0; i < 255; i++) {
		Hash256(lamportPk + i * HASH_LEN, lamport0 + i * HASH_LEN, HASH_LEN);
	}

	for (size_t i=0; i < 255; i++) {
		Hash256(lamportPk + 255 * HASH_LEN + i * HASH_LEN, lamport1 + i * HASH_LEN, HASH_LEN);
	}
	Hash256(outputLamportPk, lamportPk, HASH_LEN * 255 * 2);

	free(salt);
	free(ikm);
	free(notIkm);
	free(lamport0);
	free(lamport1);
	free(lamportPk);
}

PrivateKey DeriveChildSk(const PrivateKey& parentSk, uint32_t index) {
	uint8_t* lamportPk = AllocT<uint8_t>(HASH_LEN);
	ParentSkToLamportPK(lamportPk, parentSk, index);
	std::vector<uint8_t> lamportPkVector(lamportPk, lamportPk + HASH_LEN);
	PrivateKey child = KeyGen(Bytes(lamportPkVector));
	free(lamportPk);
	return child;
}

PrivateKey derive_path(PrivateKey sk, std::vector<uint32_t> list) {
	for (uint32_t index : list) {
		sk = DeriveChildSk(sk,index);
	}
	return sk;
}

PrivateKey master_sk_to_local_sk(PrivateKey master) {
	return derive_path(master,{12381,8444,3,0});
}

bool PrivateKey::initResult = Init();

// Construct a private key from a bytearray.
PrivateKey PrivateKey::FromBytes(const Bytes& bytes, bool modOrder)
{
	if (bytes.size() != PRIVATE_KEY_SIZE) {
		throw std::invalid_argument("PrivateKey::FromBytes: Invalid size");
	}

	PrivateKey k;
	bn_read_bin((bn_st*)k.keydata, bytes.begin(), PrivateKey::PRIVATE_KEY_SIZE);
	bn_t ord;
	bn_new(ord);
	ep_curve_get_ord(ord);
	if (modOrder) {
		bn_mod_basic((bn_st*)k.keydata, (bn_st*)k.keydata, ord);
	} else {
		if (bn_cmp((bn_st*)k.keydata, ord) > 0) {
			throw std::invalid_argument(
				"PrivateKey byte data must be less than the group order");
		}
	}
	return k;
}

// Construct a private key from a bytearray.
PrivateKey PrivateKey::FromByteVector(const std::vector<uint8_t> bytes, bool modOrder)
{
	return PrivateKey::FromBytes(Bytes(bytes), modOrder);
}

PrivateKey::PrivateKey() {
	AllocateKeyData();
};

// Construct a private key from another private key.
PrivateKey::PrivateKey(const PrivateKey &privateKey)
{
	privateKey.CheckKeyData();
	AllocateKeyData();
	bn_copy((bn_st*)keydata, (bn_st*)privateKey.keydata);
}

PrivateKey::PrivateKey(PrivateKey &&k)
	: keydata(std::exchange(k.keydata, nullptr))
{
	k.InvalidateCaches();
}

PrivateKey::~PrivateKey()
{
	DeallocateKeyData();
}

void PrivateKey::DeallocateKeyData()
{
	if(keydata != nullptr) {
		free((bn_st*)keydata);
		keydata = nullptr;
	}
	InvalidateCaches();
}

void PrivateKey::InvalidateCaches()
{
	fG1CacheValid = false;
}

PrivateKey& PrivateKey::operator=(const PrivateKey& other)
{
	CheckKeyData();
	other.CheckKeyData();
	bn_copy((bn_st*)keydata, (bn_st*)other.keydata);
	return *this;
}

PrivateKey& PrivateKey::operator=(PrivateKey&& other)
{
	DeallocateKeyData();
	keydata = (bn_st*)std::exchange(other.keydata, nullptr);
	other.InvalidateCaches();
	return *this;
}

const G1Element& PrivateKey::GetG1Element() const
{
	if (!fG1CacheValid) {
		CheckKeyData();
		ep_st *p = AllocT<ep_st>(1);
		g1_mul_gen(p, (bn_st*)keydata);

		g1Cache = G1FromNative(p);
		free(p);
		fG1CacheValid = true;
	}
	return g1Cache;
}

bool PrivateKey::IsZero() const {
	CheckKeyData();
	return bn_is_zero((bn_st*)keydata);
}

bool operator==(const PrivateKey &a, const PrivateKey &b)
{
	a.CheckKeyData();
	b.CheckKeyData();
	return bn_cmp((bn_st*)a.keydata, (bn_st*)b.keydata) == RLC_EQ;
}

bool operator!=(const PrivateKey &a, const PrivateKey &b) { return !(a == b); }

void PrivateKey::AllocateKeyData()
{
	assert(!keydata);
	keydata = AllocT<bn_st>(1);
	bn_make((bn_st*)keydata, RLC_BN_SIZE);
	bn_zero((bn_st*)keydata);
}

void PrivateKey::CheckKeyData() const
{
	if (keydata == nullptr) {
		throw std::runtime_error("PrivateKey::CheckKeyData keydata not initialized");
	}
}

void PrivateKey::Serialize(uint8_t *buffer) const
{
	if (buffer == nullptr) {
		throw std::runtime_error("PrivateKey::Serialize buffer invalid");
	}
	CheckKeyData();
	bn_write_bin(buffer, PrivateKey::PRIVATE_KEY_SIZE, (bn_st*)keydata);
}

std::vector<uint8_t> PrivateKey::Serialize() const
{
	std::vector<uint8_t> data(PRIVATE_KEY_SIZE);
	Serialize(data.data());
	return data;
}



G1Element::G1Element() { ep_set_infty(p); }

G1Element G1Element::FromBytes(const Bytes& bytes)
{
	if (bytes.size() != SIZE) {
		throw std::invalid_argument("G1Element::FromBytes: Invalid size");
	}

	G1Element ele;

	// convert bytes to relic form
	uint8_t buffer[G1Element::SIZE + 1];
	std::memcpy(buffer + 1, bytes.begin(), G1Element::SIZE);
	buffer[0] = 0x00;
	buffer[1] &= 0x1f;  // erase 3 msbs from given input

	if ((bytes[0] & 0xc0) == 0xc0) {  // representing infinity
		// enforce that infinity must be 0xc0000..00
		if (bytes[0] != 0xc0) {
			throw std::invalid_argument(
				"Given G1 infinity element must be canonical");
		}
		for (int i = 1; i < G1Element::SIZE; ++i) {
			if (bytes[i] != 0x00) {
				throw std::invalid_argument(
					"Given G1 infinity element must be canonical");
			}
		}
		return ele;
	} else {
		if ((bytes[0] & 0xc0) != 0x80) {
			throw std::invalid_argument(
				"Given G1 non-infinity element must start with 0b10");
		}

		if (bytes[0] & 0x20) {  // sign bit
			buffer[0] = 0x03;
		} else {
			buffer[0] = 0x02;
		}
	}
	ep_read_bin(ele.p, buffer, G1Element::SIZE + 1);
	ele.CheckValid();
	return ele;
}

G1Element G1Element::FromByteVector(const std::vector<uint8_t>& bytevec)
{
	return G1Element::FromBytes(Bytes(bytevec));
}


G1Element G1Element::Generator()
{
	G1Element ele;
	ep_curve_get_gen(ele.p);
	CheckRelicErrors();
	return ele;
}

bool G1Element::IsValid() const {
	// Infinity no longer valid in Relic
	// https://github.com/relic-toolkit/relic/commit/f3be2babb955cf9f82743e0ae5ef265d3da6c02b
	if (ep_is_infty((ep_st*)p) == 1)
		return true;
	return g1_is_valid((ep_st*)p);
}

void G1Element::CheckValid() const {
	if (!IsValid())
		throw std::invalid_argument("G1 element is invalid");
	CheckRelicErrors();
}



G1Element G1Element::Negate() const
{
	G1Element ans;
	ep_neg(ans.p, p);
	CheckRelicErrors();
	return ans;
}

uint32_t G1Element::GetFingerprint() const
{
	uint8_t buffer[G1Element::SIZE];
	uint8_t hash[32];
	memcpy(buffer, Serialize().data(), G1Element::SIZE);
	Hash256(hash, buffer, G1Element::SIZE);
	return FourBytesToInt(hash);
}

std::vector<uint8_t> G1Element::Serialize() const {
	uint8_t buffer[G1Element::SIZE + 1];
	ep_write_bin(buffer, G1Element::SIZE + 1, p, 1);

	if (buffer[0] == 0x00) {  // infinity
		std::vector<uint8_t> result(G1Element::SIZE, 0);
		result[0] = 0xc0;
		return result;
	}

	if (buffer[0] == 0x03) {  // sign bit set
		buffer[1] |= 0x20;
	}

	buffer[1] |= 0x80;  // indicate compression
	return std::vector<uint8_t>(buffer + 1, buffer + 1 + G1Element::SIZE);
}

bool operator==(const G1Element & a, const G1Element &b)
{
	return ep_cmp(a.p, b.p) == RLC_EQ;
}

bool operator!=(const G1Element & a, const G1Element & b) { return !(a == b); }

std::ostream& operator<<(std::ostream& os, const G1Element &ele)
{
	return os << HexStr(ele.Serialize());
}

G1Element& operator+=(G1Element& a, const G1Element& b)
{
	ep_add(a.p, a.p, b.p);
	CheckRelicErrors();
	return a;
}

G1Element operator+(const G1Element& a, const G1Element& b)
{
	G1Element ans;
	ep_add(ans.p, a.p, b.p);
	CheckRelicErrors();
	return ans;
}

G1Element operator*(const G1Element& a, const bn_t& k)
{
	G1Element ans;
	g1_mul(ans.p, (ep_st*)a.p, (bn_st*)k);
	CheckRelicErrors();
	return ans;
}

G1Element operator*(const bn_t& k, const G1Element& a) { return a * k; }