#include "Rabin.h"


namespace RABIN
{
    
    bi GenerateRabinPrime(uint64_t keySize) {
        bi prime;
        do {
            prime = generate_prime(keySize);
        } while (prime % 4 != 3);
        return prime;
    }

    
    void GenerateKeys(const std::string& publicKeyFile, const std::string& privateKeyFile, uint64_t keySize)
    {
        bi p = GenerateRabinPrime(keySize);
        bi q = GenerateRabinPrime(keySize);

        while (p == q) {q = GenerateRabinPrime(keySize);}

        bi n = p * q;

        std::cout << p << std::endl;
        std::cout << q << std::endl;
        std::cout << n << std::endl;

        WritePublicKey(publicKeyFile, n);
        WritePrivateKey(privateKeyFile, p, q);        
    }



    std::string addTagsToPlaintext(const std::string& plaintext) {
        std::string result;
        size_t tagLength = std::string(rabin_encryption_tag).length();
        size_t dataPerBlock = rabin_encryption_block_size - tagLength;
        
        size_t pos = 0;
        while (pos < plaintext.length()) {
            result += rabin_encryption_tag;

            size_t dataLength = std::min(dataPerBlock, plaintext.length() - pos);
            result += plaintext.substr(pos, dataLength);
            pos += dataLength;
        }

        return result;
    }

    

    std::vector<bi> Encrypt(const std::string& plaintextFile, const std::string& publicKeyFile)
    {
        std::ifstream file(plaintextFile);
        if (!file.is_open()) {
            printf("Failed to open plaintext file");
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string plaintext = buffer.str();
        file.close();

        auto publicKey = ReadKey(publicKeyFile);
        bi n = publicKey["n:"]; //да костыль но мне лень переделывать я тут это оставлю смотри и страдай


        plaintext = addTagsToPlaintext(plaintext);
        std::vector<uint8_t> bytes = TextToBytes(plaintext);
        bytes = PKCS7_Padding(bytes, rabin_encryption_block_size);
        
        std::vector<bi> chunks = ChunkMessage(bytes, rabin_encryption_block_size);
        
        std::vector<bi> ciphertext;
        for (const bi& chunk : chunks) {
            bi c = fast_exp_mod(chunk, 2, n);
            ciphertext.push_back(c);
        }

        return ciphertext;
    }

    
    std::string Decrypt(const std::string& ciphertextFile, const std::string& privateKeyFile) {
        auto privateKey = ReadKey(privateKeyFile);
        bi p = privateKey["p"];
        bi q = privateKey["q"];
        bi n = p * q;


        std::ifstream file(ciphertextFile);
        if (!file.is_open()) {
            printf("Failed to open ciphertext file");
            return {};
        }

        std::string line;
        std::vector<bi> ciphertext;

        while (std::getline(file, line)) {
            if (line.find("encryptedContent:") != std::string::npos) {
                break;
            }
        }

        while (std::getline(file, line)) {
            if (line.empty() || line.find("}") != std::string::npos) {
                break;
            }

            if (line.substr(0, 2) == "0x") {
                bi number;
                std::istringstream iss(line.substr(2));
                iss >> std::hex >> number;
                ciphertext.push_back(number);
            }
        }

        file.close();

        auto [gcd, yp, yq] = extended_euclidean_alg(p, q);
        std::vector<uint8_t> decryptedBytes;

        for (auto c : ciphertext)
        {
            bi mp = std::get<0>(solve_2d_congruence(c % p, p));
            bi mq = std::get<0>(solve_2d_congruence(c % q, q));

            // bi mp = fast_exp_mod(c, (p+1) / 4, p);
            // bi mq = fast_exp_mod(c, (q+1) / 4, q);

            bi m1 = (yp * p * mq + yq * q * mp) % n; if (m1 < 0) m1 += n;
            bi m2 = n - m1; if (m2 < 0) m2 += n;
            bi m3 = (yp * p * mq - yq * q * mp) % n; if (m3 < 0) m3 += n;
            bi m4 = n - m3; if (m4 < 0) m4 += n;

            for (auto m : {m1, m2, m3, m4})
            {
                std::vector<uint8_t> blockBytes = UnchunkMessage({m}, rabin_encryption_block_size);
                std::string blockText = BytesToText(blockBytes);
                if (blockText.substr(0, strlen(rabin_encryption_tag)) == rabin_encryption_tag)
                {
                    decryptedBytes.insert(decryptedBytes.end(), blockText.begin(), blockText.end());
                    break;
                }
            }
        }
        decryptedBytes = PKCS7_Unpadding(decryptedBytes);
        std::string plaintext = BytesToText(decryptedBytes);
        plaintext = RemoveRabinTags(plaintext);
        
        return plaintext;
    }


    void WritePublicKey(const std::string& publicKeyFile, const bi& n) {
        std::ofstream pubKeyStream(publicKeyFile);
        if (!pubKeyStream.is_open()) {
            std::cout << "Failed to open file for writing public key: " + publicKeyFile << "\n";
            return;
        }

        pubKeyStream << "Rabin Public Key {\n";
        pubKeyStream << "    n: " << n << "\n";
        pubKeyStream << "}\n";

        pubKeyStream.close();
    }
    
    void WritePrivateKey(const std::string& privateKeyFile, const bi& p, const bi& q) {
        std::ofstream privKeyStream(privateKeyFile);
        if (privKeyStream.is_open()) {
            privKeyStream << "Rabin Private Key {\n";
            privKeyStream << "    p   " << p << "\n";
            privKeyStream << "    q   " << q << "\n";
            privKeyStream << "}\n";
            privKeyStream.close();
        } else {
            throw std::runtime_error("Не удалось открыть файл для записи закрытого ключа.");
        }
    }


    void WriteEncryptedMessage(const std::vector<bi>& ciphertext, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        file << "RabinEncryptedMessage {\n";
        file << "    Version:    0\n";
        file << "    ContentType:    Text\n";
        file << "    ContentEncryptionAlgorithmIdentifier:    rabinEncryption\n";
        file << "    encryptedContent:\n";

        for (const bi& num : ciphertext) {
            std::stringstream hexStream;
            hexStream << std::hex << num;
            std::string hexStr = hexStream.str();
            file << "0x" << hexStr << '\n';
        }

        file << "}\n";
        file.close();
    }
    

    std::string RemoveRabinTags(const std::string& input) {
        const std::string tag = rabin_encryption_tag;
        const size_t block_size = rabin_encryption_block_size;
        std::string result;
        size_t total_blocks = input.size() / block_size + (input.size() % block_size != 0);

        for (size_t i = 0; i < total_blocks; ++i) {
            size_t start = i * block_size;
            size_t length = std::min(block_size, input.size() - start);
            std::string block = input.substr(start, length);

            if (block.compare(0, tag.size(), tag) == 0) {
                block.erase(0, tag.size());
            }

            result += block;
        }

        return result;
    }
    
}
