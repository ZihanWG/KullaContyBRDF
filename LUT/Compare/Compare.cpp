#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb-master/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

struct Options {
    std::filesystem::path baselinePath;
    std::filesystem::path candidatePath;
    std::filesystem::path outputDirectory = "ImageComparison";
    std::string inputSpace = "srgb";
    float visualizationGain = 8.0f;
    float pixelThreshold = 0.01f;
    double failMae = -1.0;
    int roiX = -1;
    int roiY = -1;
    int roiWidth = -1;
    int roiHeight = -1;
};

struct Image {
    int width = 0;
    int height = 0;
    bool sourceWasHdr = false;
    int sourceBitDepth = 8;
    std::vector<float> pixels;
};

struct Metrics {
    double rgbMae = 0.0;
    double rgbRmse = 0.0;
    double luminanceMae = 0.0;
    double luminanceRmse = 0.0;
    double maximumAbsoluteChannelError = 0.0;
    double meanBaselineLuminance = 0.0;
    double meanCandidateLuminance = 0.0;
    double relativeMeanLuminanceChange = 0.0;
    double pixelsAboveThresholdPercent = 0.0;
    double psnrDb = std::numeric_limits<double>::infinity();
};

struct ExrChannel {
    std::string name;
    int pixelType = 0;
    int bytesPerValue = 0;
};

float SrgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value) {
    value = std::max(0.0f, value);
    return value <= 0.0031308f ? 12.92f * value
                               : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float Luminance(const float* rgb) {
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

std::string EscapeJson(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    return result;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4u > data.size()) {
        throw std::runtime_error("Unexpected end of OpenEXR data");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint64_t>(ReadU32(data, offset)) |
           (static_cast<std::uint64_t>(ReadU32(data, offset + 4u)) << 32u);
}

std::int32_t ReadI32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::int32_t>(ReadU32(data, offset));
}

std::string ReadCString(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    const std::size_t start = offset;
    while (offset < data.size() && data[offset] != 0u) {
        ++offset;
    }
    if (offset >= data.size()) {
        throw std::runtime_error("Unterminated string in OpenEXR header");
    }
    const std::string result(reinterpret_cast<const char*>(data.data() + start), offset - start);
    ++offset;
    return result;
}

float HalfToFloat(std::uint16_t half) {
    const int sign = (half & 0x8000u) != 0u ? -1 : 1;
    const int exponent = static_cast<int>((half >> 10u) & 0x1Fu);
    const int mantissa = static_cast<int>(half & 0x03FFu);
    if (exponent == 0) {
        return mantissa == 0 ? (sign < 0 ? -0.0f : 0.0f)
                             : static_cast<float>(sign) * std::ldexp(static_cast<float>(mantissa), -24);
    }
    if (exponent == 31) {
        return mantissa == 0 ? static_cast<float>(sign) * std::numeric_limits<float>::infinity()
                             : std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>(sign) *
           std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, exponent - 15);
}

float ReadExrValue(const std::vector<std::uint8_t>& data,
                   std::size_t offset,
                   const ExrChannel& channel) {
    if (channel.pixelType == 1) {
        if (offset + 2u > data.size()) {
            throw std::runtime_error("Unexpected end of OpenEXR HALF channel");
        }
        const std::uint16_t half = static_cast<std::uint16_t>(data[offset]) |
                                   static_cast<std::uint16_t>(data[offset + 1u] << 8u);
        return HalfToFloat(half);
    }
    if (channel.pixelType == 2) {
        const std::uint32_t bits = ReadU32(data, offset);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    throw std::runtime_error("Unsupported OpenEXR channel type");
}

Image LoadOpenExr(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open OpenEXR image: " + path.string());
    }
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                         std::istreambuf_iterator<char>());
    if (data.size() < 9u || ReadU32(data, 0u) != 20000630u) {
        throw std::runtime_error("Not an OpenEXR file: " + path.string());
    }
    const std::uint32_t versionFlags = ReadU32(data, 4u);
    if ((versionFlags & 0x00000200u) != 0u) {
        throw std::runtime_error("Tiled OpenEXR files are not supported");
    }
    if ((versionFlags & 0x00001800u) != 0u) {
        throw std::runtime_error("Deep or multipart OpenEXR files are not supported");
    }

    std::size_t offset = 8u;
    std::map<std::string, std::vector<std::uint8_t>> attributes;
    while (offset < data.size() && data[offset] != 0u) {
        const std::string name = ReadCString(data, offset);
        (void)ReadCString(data, offset);  // Attribute type.
        const std::uint32_t size = ReadU32(data, offset);
        offset += 4u;
        if (offset + size > data.size()) {
            throw std::runtime_error("Invalid OpenEXR attribute size");
        }
        attributes[name] = std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                                      data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    }
    if (offset >= data.size()) {
        throw std::runtime_error("Invalid OpenEXR header termination");
    }
    ++offset;

    const auto requireAttribute = [&](const char* name) -> const std::vector<std::uint8_t>& {
        const auto found = attributes.find(name);
        if (found == attributes.end()) {
            throw std::runtime_error(std::string("OpenEXR is missing attribute: ") + name);
        }
        return found->second;
    };
    const auto& compressionAttribute = requireAttribute("compression");
    const auto& lineOrderAttribute = requireAttribute("lineOrder");
    const auto& dataWindow = requireAttribute("dataWindow");
    const auto& channelAttribute = requireAttribute("channels");
    if (compressionAttribute.empty() || lineOrderAttribute.empty() || dataWindow.size() < 16u) {
        throw std::runtime_error("Malformed OpenEXR attributes");
    }
    const int compression = compressionAttribute[0];
    if (compression != 0 && compression != 2 && compression != 3) {
        throw std::runtime_error("OpenEXR compression must be NONE, ZIPS, or ZIP");
    }
    if (lineOrderAttribute[0] != 0u) {
        throw std::runtime_error("Only increasing OpenEXR scanline order is supported");
    }

    const int minimumX = ReadI32(dataWindow, 0u);
    const int minimumY = ReadI32(dataWindow, 4u);
    const int maximumX = ReadI32(dataWindow, 8u);
    const int maximumY = ReadI32(dataWindow, 12u);
    const int width = maximumX - minimumX + 1;
    const int height = maximumY - minimumY + 1;
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid OpenEXR data window");
    }

    std::vector<ExrChannel> channels;
    std::size_t channelOffset = 0u;
    while (channelOffset < channelAttribute.size() && channelAttribute[channelOffset] != 0u) {
        ExrChannel channel;
        channel.name = ReadCString(channelAttribute, channelOffset);
        if (channelOffset + 16u > channelAttribute.size()) {
            throw std::runtime_error("Malformed OpenEXR channel list");
        }
        channel.pixelType = ReadI32(channelAttribute, channelOffset);
        const int xSampling = ReadI32(channelAttribute, channelOffset + 8u);
        const int ySampling = ReadI32(channelAttribute, channelOffset + 12u);
        channelOffset += 16u;
        if ((channel.pixelType != 1 && channel.pixelType != 2) || xSampling != 1 ||
            ySampling != 1) {
            throw std::runtime_error("OpenEXR channels must be unsampled HALF or FLOAT");
        }
        channel.bytesPerValue = channel.pixelType == 1 ? 2 : 4;
        channels.push_back(channel);
    }
    if (channels.empty()) {
        throw std::runtime_error("OpenEXR has no channels");
    }

    const int linesPerChunk = compression == 3 ? 16 : 1;
    const int chunkCount = (height + linesPerChunk - 1) / linesPerChunk;
    if (offset + static_cast<std::size_t>(chunkCount) * 8u > data.size()) {
        throw std::runtime_error("OpenEXR chunk table is truncated");
    }

    Image image;
    image.width = width;
    image.height = height;
    image.sourceWasHdr = true;
    image.sourceBitDepth = 16;
    image.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u,
                        0.0f);
    bool hasRed = false;
    bool hasGreen = false;
    bool hasBlue = false;
    for (const ExrChannel& channel : channels) {
        image.sourceBitDepth = std::max(image.sourceBitDepth, channel.bytesPerValue * 8);
        hasRed = hasRed || channel.name == "R";
        hasGreen = hasGreen || channel.name == "G";
        hasBlue = hasBlue || channel.name == "B";
    }
    if (!hasRed || !hasGreen || !hasBlue) {
        throw std::runtime_error("OpenEXR comparison inputs must contain R, G, and B channels");
    }

    for (int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        const std::uint64_t chunkFileOffset =
            ReadU64(data, offset + static_cast<std::size_t>(chunkIndex) * 8u);
        if (chunkFileOffset > data.size() - 8u) {
            throw std::runtime_error("Invalid OpenEXR chunk offset");
        }
        const int firstY = ReadI32(data, static_cast<std::size_t>(chunkFileOffset));
        const int packedSize = ReadI32(data, static_cast<std::size_t>(chunkFileOffset) + 4u);
        if (packedSize < 0 || static_cast<std::size_t>(chunkFileOffset) + 8u +
                                    static_cast<std::size_t>(packedSize) > data.size()) {
            throw std::runtime_error("Invalid OpenEXR chunk size");
        }
        const int lineCount = std::min(linesPerChunk, maximumY - firstY + 1);
        int bytesPerPixel = 0;
        for (const ExrChannel& channel : channels) {
            bytesPerPixel += channel.bytesPerValue;
        }
        const std::size_t expectedSize = static_cast<std::size_t>(lineCount) *
                                         static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(bytesPerPixel);
        std::vector<std::uint8_t> raw(expectedSize);
        const std::size_t packedOffset = static_cast<std::size_t>(chunkFileOffset) + 8u;
        if (compression == 0 || static_cast<std::size_t>(packedSize) == expectedSize) {
            if (static_cast<std::size_t>(packedSize) != expectedSize) {
                throw std::runtime_error("Unexpected uncompressed OpenEXR chunk size");
            }
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(packedOffset), expectedSize,
                        raw.begin());
        } else {
            std::vector<std::uint8_t> predicted(expectedSize);
            const int decodedSize = stbi_zlib_decode_buffer(
                reinterpret_cast<char*>(predicted.data()), static_cast<int>(predicted.size()),
                reinterpret_cast<const char*>(data.data() + packedOffset), packedSize);
            if (decodedSize != static_cast<int>(expectedSize)) {
                throw std::runtime_error("Failed to decompress OpenEXR ZIP chunk");
            }
            for (std::size_t byteIndex = 1u; byteIndex < predicted.size(); ++byteIndex) {
                predicted[byteIndex] = static_cast<std::uint8_t>(
                    static_cast<unsigned int>(predicted[byteIndex - 1u]) +
                    static_cast<unsigned int>(predicted[byteIndex]) - 128u);
            }
            const std::size_t split = (predicted.size() + 1u) / 2u;
            for (std::size_t byteIndex = 0u; byteIndex < raw.size(); ++byteIndex) {
                raw[byteIndex] = byteIndex % 2u == 0u
                                     ? predicted[byteIndex / 2u]
                                     : predicted[split + byteIndex / 2u];
            }
        }

        std::size_t rawOffset = 0u;
        for (int y = firstY; y < firstY + lineCount; ++y) {
            const int targetY = y - minimumY;
            if (targetY < 0 || targetY >= height) {
                throw std::runtime_error("OpenEXR chunk scanline is outside the data window");
            }
            for (const ExrChannel& channel : channels) {
                int targetChannel = -1;
                if (channel.name == "R") {
                    targetChannel = 0;
                } else if (channel.name == "G") {
                    targetChannel = 1;
                } else if (channel.name == "B") {
                    targetChannel = 2;
                }
                for (int x = 0; x < width; ++x) {
                    if (targetChannel >= 0) {
                        const std::size_t destination =
                            (static_cast<std::size_t>(targetY) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) * 3u +
                            static_cast<std::size_t>(targetChannel);
                        image.pixels[destination] = ReadExrValue(raw, rawOffset, channel);
                    }
                    rawOffset += static_cast<std::size_t>(channel.bytesPerValue);
                }
            }
        }
        if (rawOffset != raw.size()) {
            throw std::runtime_error("Unexpected OpenEXR decoded layout");
        }
    }
    return image;
}

float ParsePositiveFloat(const std::string& text, const char* optionName, bool allowZero) {
    std::size_t parsedCharacters = 0;
    const float value = std::stof(text, &parsedCharacters);
    if (parsedCharacters != text.size() || !std::isfinite(value) ||
        (allowZero ? value < 0.0f : value <= 0.0f)) {
        throw std::runtime_error(std::string("Invalid value for ") + optionName + ": " + text);
    }
    return value;
}

void ParseRoi(const std::string& text, Options& options) {
    std::istringstream stream(text);
    char separator1 = 0;
    char separator2 = 0;
    char separator3 = 0;
    if (!(stream >> options.roiX >> separator1 >> options.roiY >> separator2 >>
          options.roiWidth >> separator3 >> options.roiHeight) ||
        separator1 != ',' || separator2 != ',' || separator3 != ',' || options.roiX < 0 ||
        options.roiY < 0 || options.roiWidth <= 0 || options.roiHeight <= 0) {
        throw std::runtime_error("--roi must be x,y,width,height with a positive size");
    }
    stream >> std::ws;
    if (!stream.eof()) {
        throw std::runtime_error("Unexpected trailing characters in --roi");
    }
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto requireValue = [&](const char* optionName) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + optionName);
            }
            return argv[++index];
        };

        if (argument == "--baseline") {
            options.baselinePath = requireValue("--baseline");
        } else if (argument == "--candidate") {
            options.candidatePath = requireValue("--candidate");
        } else if (argument == "--output") {
            options.outputDirectory = requireValue("--output");
        } else if (argument == "--input-space") {
            options.inputSpace = requireValue("--input-space");
            if (options.inputSpace != "srgb" && options.inputSpace != "linear") {
                throw std::runtime_error("--input-space must be srgb or linear");
            }
        } else if (argument == "--gain") {
            options.visualizationGain = ParsePositiveFloat(requireValue("--gain"), "--gain", false);
        } else if (argument == "--pixel-threshold") {
            options.pixelThreshold =
                ParsePositiveFloat(requireValue("--pixel-threshold"), "--pixel-threshold", true);
        } else if (argument == "--fail-mae") {
            options.failMae = ParsePositiveFloat(requireValue("--fail-mae"), "--fail-mae", true);
        } else if (argument == "--roi") {
            ParseRoi(requireValue("--roi"), options);
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "Usage: KullaContyImageCompare --baseline image --candidate image [options]\n"
                << "Supports LDR images, Radiance HDR, and scanline RGB EXR (NONE/ZIPS/ZIP).\n"
                << "  --output directory       Output directory (default: ImageComparison)\n"
                << "  --input-space srgb|linear  Encoding of LDR inputs (HDR is always linear)\n"
                << "  --gain number            Difference visualization gain (default: 8)\n"
                << "  --pixel-threshold number Fractional linear RGB threshold (default: 0.01)\n"
                << "  --fail-mae number        Return non-zero when RGB MAE exceeds this value\n"
                << "  --roi x,y,width,height   Compare and export only this aligned region\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + argument);
        }
    }

    if (options.baselinePath.empty() || options.candidatePath.empty()) {
        throw std::runtime_error("--baseline and --candidate are required");
    }
    return options;
}

Image LoadImage(const std::filesystem::path& path, const std::string& inputSpace) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Input image does not exist: " + path.string());
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".exr") {
        return LoadOpenExr(path);
    }

    Image image;
    int sourceChannels = 0;
    const std::string filename = path.string();
    image.sourceWasHdr = stbi_is_hdr(filename.c_str()) != 0;
    if (image.sourceWasHdr) {
        image.sourceBitDepth = 32;
        float* source = stbi_loadf(filename.c_str(), &image.width, &image.height, &sourceChannels, 3);
        if (source == nullptr) {
            throw std::runtime_error("Failed to load HDR image: " + filename + " (" +
                                     stbi_failure_reason() + ")");
        }
        const std::size_t valueCount = static_cast<std::size_t>(image.width) *
                                       static_cast<std::size_t>(image.height) * 3u;
        image.pixels.assign(source, source + valueCount);
        stbi_image_free(source);
    } else if (stbi_is_16_bit(filename.c_str()) != 0) {
        image.sourceBitDepth = 16;
        stbi_us* source =
            stbi_load_16(filename.c_str(), &image.width, &image.height, &sourceChannels, 3);
        if (source == nullptr) {
            throw std::runtime_error("Failed to load 16-bit image: " + filename + " (" +
                                     stbi_failure_reason() + ")");
        }
        const std::size_t valueCount = static_cast<std::size_t>(image.width) *
                                       static_cast<std::size_t>(image.height) * 3u;
        image.pixels.resize(valueCount);
        for (std::size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex) {
            const float encoded = static_cast<float>(source[valueIndex]) / 65535.0f;
            image.pixels[valueIndex] = inputSpace == "srgb" ? SrgbToLinear(encoded) : encoded;
        }
        stbi_image_free(source);
    } else {
        stbi_uc* source = stbi_load(filename.c_str(), &image.width, &image.height, &sourceChannels, 3);
        if (source == nullptr) {
            throw std::runtime_error("Failed to load LDR image: " + filename + " (" +
                                     stbi_failure_reason() + ")");
        }
        const std::size_t valueCount = static_cast<std::size_t>(image.width) *
                                       static_cast<std::size_t>(image.height) * 3u;
        image.pixels.resize(valueCount);
        for (std::size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex) {
            const float encoded = static_cast<float>(source[valueIndex]) / 255.0f;
            image.pixels[valueIndex] = inputSpace == "srgb" ? SrgbToLinear(encoded) : encoded;
        }
        stbi_image_free(source);
    }
    return image;
}

Image CropImage(const Image& source, const Options& options) {
    if (options.roiX < 0) {
        return source;
    }
    if (options.roiX + options.roiWidth > source.width ||
        options.roiY + options.roiHeight > source.height) {
        throw std::runtime_error("--roi extends outside an input image");
    }

    Image cropped;
    cropped.width = options.roiWidth;
    cropped.height = options.roiHeight;
    cropped.sourceWasHdr = source.sourceWasHdr;
    cropped.sourceBitDepth = source.sourceBitDepth;
    cropped.pixels.resize(static_cast<std::size_t>(cropped.width) *
                          static_cast<std::size_t>(cropped.height) * 3u);
    for (int y = 0; y < cropped.height; ++y) {
        const std::size_t sourceOffset =
            (static_cast<std::size_t>(options.roiY + y) * static_cast<std::size_t>(source.width) +
             static_cast<std::size_t>(options.roiX)) * 3u;
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(cropped.width) * 3u;
        std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                    static_cast<std::size_t>(cropped.width) * 3u,
                    cropped.pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
    }
    return cropped;
}

Metrics CalculateMetrics(const Image& baseline, const Image& candidate, float pixelThreshold) {
    if (baseline.width != candidate.width || baseline.height != candidate.height) {
        throw std::runtime_error("Images must have identical dimensions for a pixel-aligned comparison");
    }

    Metrics metrics;
    const std::size_t pixelCount = static_cast<std::size_t>(baseline.width) *
                                   static_cast<std::size_t>(baseline.height);
    double rgbAbsoluteSum = 0.0;
    double rgbSquaredSum = 0.0;
    double luminanceAbsoluteSum = 0.0;
    double luminanceSquaredSum = 0.0;
    std::size_t pixelsAboveThreshold = 0;

    for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const float* baselinePixel = &baseline.pixels[pixelIndex * 3u];
        const float* candidatePixel = &candidate.pixels[pixelIndex * 3u];
        float pixelMaximumError = 0.0f;
        for (int channel = 0; channel < 3; ++channel) {
            if (!std::isfinite(baselinePixel[channel]) ||
                !std::isfinite(candidatePixel[channel])) {
                throw std::runtime_error("Input images contain a non-finite RGB value");
            }
            const double difference = static_cast<double>(candidatePixel[channel]) -
                                      static_cast<double>(baselinePixel[channel]);
            const double absoluteDifference = std::abs(difference);
            rgbAbsoluteSum += absoluteDifference;
            rgbSquaredSum += difference * difference;
            metrics.maximumAbsoluteChannelError =
                std::max(metrics.maximumAbsoluteChannelError, absoluteDifference);
            pixelMaximumError = std::max(pixelMaximumError, static_cast<float>(absoluteDifference));
        }
        if (pixelMaximumError > pixelThreshold) {
            ++pixelsAboveThreshold;
        }

        const double baselineLuminance = Luminance(baselinePixel);
        const double candidateLuminance = Luminance(candidatePixel);
        const double luminanceDifference = candidateLuminance - baselineLuminance;
        luminanceAbsoluteSum += std::abs(luminanceDifference);
        luminanceSquaredSum += luminanceDifference * luminanceDifference;
        metrics.meanBaselineLuminance += baselineLuminance;
        metrics.meanCandidateLuminance += candidateLuminance;
    }

    const double channelValueCount = static_cast<double>(pixelCount) * 3.0;
    metrics.rgbMae = rgbAbsoluteSum / channelValueCount;
    metrics.rgbRmse = std::sqrt(rgbSquaredSum / channelValueCount);
    metrics.luminanceMae = luminanceAbsoluteSum / static_cast<double>(pixelCount);
    metrics.luminanceRmse = std::sqrt(luminanceSquaredSum / static_cast<double>(pixelCount));
    metrics.meanBaselineLuminance /= static_cast<double>(pixelCount);
    metrics.meanCandidateLuminance /= static_cast<double>(pixelCount);
    if (std::abs(metrics.meanBaselineLuminance) > 1.0e-12) {
        metrics.relativeMeanLuminanceChange =
            (metrics.meanCandidateLuminance - metrics.meanBaselineLuminance) /
            metrics.meanBaselineLuminance * 100.0;
    }
    metrics.pixelsAboveThresholdPercent =
        static_cast<double>(pixelsAboveThreshold) / static_cast<double>(pixelCount) * 100.0;
    if (metrics.rgbRmse > 0.0 && !baseline.sourceWasHdr && !candidate.sourceWasHdr) {
        metrics.psnrDb = 20.0 * std::log10(1.0 / metrics.rgbRmse);
    }
    return metrics;
}

std::uint8_t ToByte(float linearValue) {
    const float encoded = std::clamp(LinearToSrgb(linearValue), 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
}

void WriteDifferenceImages(const Image& baseline,
                           const Image& candidate,
                           const std::filesystem::path& outputDirectory,
                           float gain) {
    const std::size_t pixelCount = static_cast<std::size_t>(baseline.width) *
                                   static_cast<std::size_t>(baseline.height);
    std::vector<float> absoluteHdr(pixelCount * 3u);
    std::vector<std::uint8_t> absolutePng(pixelCount * 3u);
    std::vector<std::uint8_t> signedPng(pixelCount * 3u);

    for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const float* baselinePixel = &baseline.pixels[pixelIndex * 3u];
        const float* candidatePixel = &candidate.pixels[pixelIndex * 3u];
        for (int channel = 0; channel < 3; ++channel) {
            const std::size_t valueIndex = pixelIndex * 3u + static_cast<std::size_t>(channel);
            const float difference = std::abs(candidatePixel[channel] - baselinePixel[channel]);
            absoluteHdr[valueIndex] = difference;
            absolutePng[valueIndex] = ToByte(difference * gain);
        }

        const float luminanceDifference = Luminance(candidatePixel) - Luminance(baselinePixel);
        const float positive = std::max(0.0f, luminanceDifference) * gain;
        const float negative = std::max(0.0f, -luminanceDifference) * gain;
        signedPng[pixelIndex * 3u + 0u] = ToByte(positive);
        signedPng[pixelIndex * 3u + 1u] = 0;
        signedPng[pixelIndex * 3u + 2u] = ToByte(negative);
    }

    const std::filesystem::path absolutePngPath = outputDirectory / "absolute_difference.png";
    const std::filesystem::path signedPngPath = outputDirectory / "signed_luminance_difference.png";
    const std::filesystem::path absoluteHdrPath = outputDirectory / "absolute_difference.hdr";
    if (stbi_write_png(absolutePngPath.string().c_str(), baseline.width, baseline.height, 3,
                       absolutePng.data(), baseline.width * 3) == 0 ||
        stbi_write_png(signedPngPath.string().c_str(), baseline.width, baseline.height, 3,
                       signedPng.data(), baseline.width * 3) == 0 ||
        stbi_write_hdr(absoluteHdrPath.string().c_str(), baseline.width, baseline.height, 3,
                       absoluteHdr.data()) == 0) {
        throw std::runtime_error("Failed to write one or more difference images");
    }
}

void WriteMetrics(const Options& options,
                  const Image& baseline,
                  const Image& candidate,
                  const Metrics& metrics) {
    std::ofstream stream(options.outputDirectory / "metrics.json", std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to create metrics.json");
    }
    stream << std::fixed << std::setprecision(10)
           << "{\n"
           << "  \"baseline\": \"" << EscapeJson(options.baselinePath.generic_string()) << "\",\n"
           << "  \"candidate\": \"" << EscapeJson(options.candidatePath.generic_string()) << "\",\n"
           << "  \"width\": " << baseline.width << ",\n"
           << "  \"height\": " << baseline.height << ",\n"
           << "  \"baseline_hdr\": " << (baseline.sourceWasHdr ? "true" : "false") << ",\n"
           << "  \"candidate_hdr\": " << (candidate.sourceWasHdr ? "true" : "false") << ",\n"
           << "  \"baseline_source_bit_depth\": " << baseline.sourceBitDepth << ",\n"
           << "  \"candidate_source_bit_depth\": " << candidate.sourceBitDepth << ",\n"
           << "  \"roi\": ";
    if (options.roiX >= 0) {
        stream << "{\"x\": " << options.roiX << ", \"y\": " << options.roiY
               << ", \"width\": " << options.roiWidth << ", \"height\": "
               << options.roiHeight << "},\n";
    } else {
        stream << "null,\n";
    }
    stream
           << "  \"ldr_input_space\": \"" << options.inputSpace << "\",\n"
           << "  \"visualization_gain\": " << options.visualizationGain << ",\n"
           << "  \"pixel_threshold\": " << options.pixelThreshold << ",\n"
           << "  \"rgb_mae\": " << metrics.rgbMae << ",\n"
           << "  \"rgb_rmse\": " << metrics.rgbRmse << ",\n"
           << "  \"luminance_mae\": " << metrics.luminanceMae << ",\n"
           << "  \"luminance_rmse\": " << metrics.luminanceRmse << ",\n"
           << "  \"maximum_absolute_channel_error\": "
           << metrics.maximumAbsoluteChannelError << ",\n"
           << "  \"mean_baseline_luminance\": " << metrics.meanBaselineLuminance << ",\n"
           << "  \"mean_candidate_luminance\": " << metrics.meanCandidateLuminance << ",\n"
           << "  \"relative_mean_luminance_change_percent\": "
           << metrics.relativeMeanLuminanceChange << ",\n"
           << "  \"pixels_above_threshold_percent\": "
           << metrics.pixelsAboveThresholdPercent << ",\n"
           << "  \"psnr_db\": ";
    if (std::isfinite(metrics.psnrDb)) {
        stream << metrics.psnrDb;
    } else {
        stream << "null";
    }
    stream << "\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = ParseOptions(argc, argv);
        const Image baseline = CropImage(LoadImage(options.baselinePath, options.inputSpace), options);
        const Image candidate = CropImage(LoadImage(options.candidatePath, options.inputSpace), options);
        const Metrics metrics = CalculateMetrics(baseline, candidate, options.pixelThreshold);

        std::filesystem::create_directories(options.outputDirectory);
        WriteDifferenceImages(baseline, candidate, options.outputDirectory,
                              options.visualizationGain);
        WriteMetrics(options, baseline, candidate, metrics);

        std::cout << std::fixed << std::setprecision(8)
                  << "RGB MAE: " << metrics.rgbMae << '\n'
                  << "RGB RMSE: " << metrics.rgbRmse << '\n'
                  << "Luminance MAE: " << metrics.luminanceMae << '\n'
                  << "Mean luminance change: " << metrics.relativeMeanLuminanceChange << "%\n"
                  << "Pixels above threshold: " << metrics.pixelsAboveThresholdPercent << "%\n"
                  << "Wrote comparison artifacts to " << options.outputDirectory << '\n';

        if (options.failMae >= 0.0 && metrics.rgbMae > options.failMae) {
            std::cerr << "RGB MAE exceeded --fail-mae threshold " << options.failMae << '\n';
            return 2;
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
