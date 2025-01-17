#pragma once

#include <limits>
#include <ratio>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <bitset>
#include <print>
#include <cmath>

class huffman {
public:
    using encoding_map = std::unordered_map<char, std::pair<uint32_t, int>>;
    // Constructor that takes the encoding map
    huffman(const encoding_map& encodings)
        : encodings_(encodings) {
        // Create a reverse map for decoding
        for (const auto& pair : encodings) {
            auto codepoint = pair.first;
            auto length = pair.second.second;
            auto encoding = pair.second.first;// << (32 - length);
            reverse_map_ [encoding] = codepoint; // Store character
            code_lengths_[encoding] = length; // Store length
        }
    }

    std::string decode(const std::string_view& data) {
        std::string output;
        auto     pos = data.begin();
        auto end = data.end();

        size_t min_bit_size = std::numeric_limits<size_t>::max();
        for (auto &[_, size] : code_lengths_) {
            if (static_cast<size_t>(size) < min_bit_size) {
                min_bit_size = size;
            }
        }

        uint32_t code = 0;

        size_t bits_checked = 0;
        while (pos != end) {
            uint8_t byte = static_cast<uint8_t>(*pos);
            for (int i = 7; i >= 0; --i) {
                uint8_t bit = (byte >> i) & 1; // Extract the bit
                code = (code << 1) | bit; // Shift left and add the new bit
                bits_checked++;

                // Check if we have a valid code
                if (code_lengths_.contains(code) && bits_checked >= min_bit_size) {
                    auto len = code_lengths_[code];
                    if((size_t) len == bits_checked) {
                        output += reverse_map_[code];
                        code = 0; // Reset code after successful decode
                        bits_checked = 0; // Reset bits checked
                    }
                }

                // If we have checked enough bits, we can reset
                if (bits_checked >= 32) {
                    code = 0; // Reset code if we exceed 32 bits
                    bits_checked = 0; // Reset bits checked
                }
            }
            pos++;
        }
        return output;
    }

    std::vector<std::byte> encode(const std::string& input) {
        std::vector<std::byte> output;

        size_t bit_position = 0;
        uint8_t byte = 0;

        for (const char codepoint : input) {
            auto code = encodings_[codepoint].first; // Get the Huffman code
            auto length = code_lengths_[code]; // Get the length of the Huffman code

            for (int i = 0; i < length; ++i) {
                byte <<= 1; // Shift left to make space for the new bit
                byte |= (code >> (length - 1 - i)) & 1; // Add the new bit

                bit_position++;
                if (bit_position == 8) { // If we have filled a byte
                    output.push_back(static_cast<std::byte>(byte)); // Store the byte
                    byte = 0; // Reset byte
                    bit_position = 0; // Reset bit position
                }
            }
        }

        // Handle any remaining bits that didn't fill a complete byte
// Handle any remaining bits that didn't fill a complete byte
        if (bit_position > 0) {
            // Shift remaining bits to the left to fill the byte
            byte <<= (8 - bit_position); // Shift remaining bits to the left

            // Set the remaining bits to 1
            byte |= (1 << (8 - bit_position)) - 1; // Set the last (8 - bit_position) bits to 1

            output.push_back(static_cast<std::byte>(byte)); // Store the last byte
        }

        return output;
    }



private:
    encoding_map encodings_; // Original encoding map with lengths
    std::unordered_map<uint32_t, char> reverse_map_;  // Reverse map for decoding
    std::unordered_map<uint32_t, int> code_lengths_;   // Lengths of the codes
};
