#ifndef SRC_KEYGEN_HPP
#define SRC_KEYGEN_HPP
#include <iomanip>
#include <vector>
#include <gmp.h>
extern "C" {
#include "relic/include/relic_conf.h"
#include "relic/include/relic.h"
}


extern void Hash256(uint8_t* output, const uint8_t* message, size_t messageLen);

class Bytes {
    const uint8_t* pData;
    const size_t nSize;

public:
    explicit Bytes(const uint8_t* pDataIn, const size_t nSizeIn)
        : pData(pDataIn), nSize(nSizeIn)
    {
    }
    explicit Bytes(const std::vector<uint8_t>& vecBytes)
        : pData(vecBytes.data()), nSize(vecBytes.size())
    {
    }

    inline const uint8_t* begin() const { return pData; }
    inline const uint8_t* end() const { return pData + nSize; }

    inline size_t size() const { return nSize; }

    const uint8_t& operator[](const int nIndex) const { return pData[nIndex]; }
};

class G1Element {
public:
    static const size_t SIZE = 48;
    G1Element();
    static G1Element FromBytes(const Bytes& bytes);
    static G1Element FromByteVector(const std::vector<uint8_t> &bytevec);
    static G1Element Generator();

    bool IsValid() const;
    void CheckValid() const;
    G1Element Negate() const;
    uint32_t GetFingerprint() const;
    std::vector<uint8_t> Serialize() const;

    friend bool operator==(const G1Element &a, const G1Element &b);
    friend bool operator!=(const G1Element &a, const G1Element &b);
    friend std::ostream &operator<<(std::ostream &os, const G1Element &s);
    friend G1Element& operator+=(G1Element& a, const G1Element& b);
    friend G1Element operator+(const G1Element &a, const G1Element &b);

    ep_t p;
};

class PrivateKey {
 public:
    // Private keys are represented as 32 byte field elements. Note that
    // not all 32 byte integers are valid keys, the private key must be
    // less than the group order (which is in bls.hpp).
    static const size_t PRIVATE_KEY_SIZE = 32;
	static bool initResult;

    // Construct a private key from a bytearray.
    static PrivateKey FromBytes(const Bytes& bytes, bool modOrder = false);

    // Construct a private key from a bytearray.
    static PrivateKey FromByteVector(const std::vector<uint8_t> bytes, bool modOrder = false);

    // Aggregate many private keys into one (sum of keys mod order)
    static PrivateKey Aggregate(std::vector<PrivateKey> const &privateKeys);

    // Construct a private key from another private key. Allocates memory in
    // secure heap, and copies keydata.
    PrivateKey(const PrivateKey &k);
    PrivateKey(PrivateKey &&k);

    PrivateKey& operator=(const PrivateKey& other);
    PrivateKey& operator=(PrivateKey&& other);

    ~PrivateKey();

    const G1Element& GetG1Element() const;

    bool IsZero() const;

    // Compare to different private key
    friend bool operator==(const PrivateKey &a, const PrivateKey &b);
    friend bool operator!=(const PrivateKey &a, const PrivateKey &b);

    // Serialize the key into bytes
    void Serialize(uint8_t *buffer) const;
    std::vector<uint8_t> Serialize() const;
    
    void* keydata{nullptr};

 private:
    // Don't allow public construction, force static methods
    PrivateKey();

    // Allocate memory for private key
    void AllocateKeyData();
    /// Throw an error if keydata isn't initialized
    void CheckKeyData() const;
    /// Deallocate *keydata and keydata if requried
    void DeallocateKeyData();

    void InvalidateCaches();

    // The actual byte data


    mutable bool fG1CacheValid{false};
    mutable G1Element g1Cache;
};

static const uint8_t HASH_LEN = PrivateKey::PRIVATE_KEY_SIZE;
extern PrivateKey KeyGen(const Bytes& seed);
extern PrivateKey master_sk_to_local_sk(PrivateKey master);

#endif