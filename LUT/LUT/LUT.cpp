#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinPerceptualRoughness = 0.002f;

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct IntegratedBRDF {
    float splitA = 0.0f;
    float splitB = 0.0f;
    float directionalAlbedo = 0.0f;
};

struct ValidationMetrics {
    double whiteFurnaceMaxError = 0.0;
    double whiteFurnaceRmsError = 0.0;
    double gauss4AverageMaxError = 0.0;
    double gauss4AverageRmsError = 0.0;
    double maximumColoredDirectionalAlbedo = 0.0;
    bool passed = false;
};

struct Options {
    int size = 256;
    int sampleCount = 1024;
    std::filesystem::path outputDirectory = "Generated";
};

float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Normalize(const Vec3& value) {
    const float lengthSquared = Dot(value, value);
    if (lengthSquared <= 0.0f) {
        return {0.0f, 0.0f, 1.0f};
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float Pow5(float value) {
    const float valueSquared = value * value;
    return valueSquared * valueSquared * value;
}

float RadicalInverseVdC(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec2 Hammersley(std::uint32_t index, std::uint32_t count) {
    return {static_cast<float>(index) / static_cast<float>(count), RadicalInverseVdC(index)};
}

// Heitz 2018 visible-normal sampling for an isotropic GGX distribution.
// Sampling the VNDF changes the directional-albedo estimator to G1(L), which
// has much lower variance than sampling the full NDF at smooth grazing angles.
Vec3 ImportanceSampleGGXVNDF(const Vec3& view, const Vec2& xi, float alpha) {
    const Vec3 stretchedView = Normalize({alpha * view.x, alpha * view.y, view.z});

    const float lensq = stretchedView.x * stretchedView.x +
                        stretchedView.y * stretchedView.y;
    const Vec3 tangent1 = lensq > 0.0f
        ? Vec3{-stretchedView.y / std::sqrt(lensq),
               stretchedView.x / std::sqrt(lensq), 0.0f}
        : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 tangent2 = Cross(stretchedView, tangent1);

    const float radius = std::sqrt(xi.x);
    const float phi = 2.0f * kPi * xi.y;
    const float t1 = radius * std::cos(phi);
    float t2 = radius * std::sin(phi);
    const float blend = 0.5f * (1.0f + stretchedView.z);
    t2 = (1.0f - blend) * std::sqrt(std::max(1.0f - t1 * t1, 0.0f)) +
         blend * t2;

    const float normalComponent =
        std::sqrt(std::max(1.0f - t1 * t1 - t2 * t2, 0.0f));
    const Vec3 visibleNormal{
        t1 * tangent1.x + t2 * tangent2.x + normalComponent * stretchedView.x,
        t1 * tangent1.y + t2 * tangent2.y + normalComponent * stretchedView.y,
        t1 * tangent1.z + t2 * tangent2.z + normalComponent * stretchedView.z,
    };

    return Normalize({alpha * visibleNormal.x,
                      alpha * visibleNormal.y,
                      std::max(visibleNormal.z, 0.0f)});
}

// Exact separable Smith masking-shadowing for isotropic GGX. The runtime HLSL
// uses the same function so the precomputed energy matches the evaluated BRDF.
float SmithG1GGX(float noX, float alpha) {
    if (noX <= 0.0f) {
        return 0.0f;
    }
    const float alphaSquared = alpha * alpha;
    const float root = std::sqrt(alphaSquared + (1.0f - alphaSquared) * noX * noX);
    return (2.0f * noX) / (noX + root);
}

IntegratedBRDF IntegrateBRDF(float noV, float perceptualRoughness, int sampleCount) {
    const float roughness = std::max(perceptualRoughness, kMinPerceptualRoughness);
    const float alpha = roughness * roughness;
    const Vec3 view{std::sqrt(std::max(1.0f - noV * noV, 0.0f)), 0.0f, noV};

    IntegratedBRDF result;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const Vec2 xi = Hammersley(static_cast<std::uint32_t>(sampleIndex),
                                   static_cast<std::uint32_t>(sampleCount));
        const Vec3 halfVector = ImportanceSampleGGXVNDF(view, xi, alpha);
        const float voH = std::max(Dot(view, halfVector), 0.0f);
        const Vec3 light = Normalize({
            2.0f * voH * halfVector.x - view.x,
            2.0f * voH * halfVector.y - view.y,
            2.0f * voH * halfVector.z - view.z,
        });

        const float noL = std::max(light.z, 0.0f);
        if (noL <= 0.0f || voH <= 0.0f) {
            continue;
        }

        // For GGX VNDF sampling, f_ss * NoL / pdf(L) simplifies to G1(L).
        const float visibilityWeight = SmithG1GGX(noL, alpha);
        const float fresnelCoefficient = Pow5(1.0f - voH);

        result.splitA += (1.0f - fresnelCoefficient) * visibilityWeight;
        result.splitB += fresnelCoefficient * visibilityWeight;
        result.directionalAlbedo += visibilityWeight; // Fresnel is one for E(mu).
    }

    const float inverseSampleCount = 1.0f / static_cast<float>(sampleCount);
    result.splitA *= inverseSampleCount;
    result.splitB *= inverseSampleCount;
    result.directionalAlbedo *= inverseSampleCount;
    return result;
}

void SetRGB(std::vector<float>& image, int width, int x, int y, float r, float g, float b) {
    const std::size_t offset = static_cast<std::size_t>((y * width + x) * 3);
    image[offset + 0] = r;
    image[offset + 1] = g;
    image[offset + 2] = b;
}

bool WriteHDR(const std::filesystem::path& path, int width, int height,
              const std::vector<float>& data) {
    return stbi_write_hdr(path.string().c_str(), width, height, 3, data.data()) != 0;
}

bool WritePreviewPNG(const std::filesystem::path& path, int width, int height,
                     const std::vector<float>& data) {
    std::vector<std::uint8_t> preview(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        const float value = std::clamp(data[i], 0.0f, 1.0f);
        preview[i] = static_cast<std::uint8_t>(std::lround(value * 255.0f));
    }
    return stbi_write_png(path.string().c_str(), width, height, 3, preview.data(), width * 3) != 0;
}

void AppendBigEndian32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

std::uint32_t CRC32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

std::uint32_t Adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t modulus = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (const std::uint8_t value : data) {
        a = (a + value) % modulus;
        b = (b + a) % modulus;
    }
    return (b << 16u) | a;
}

bool WritePNGChunk(std::ofstream& stream, const char type[4],
                   const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> lengthBytes;
    AppendBigEndian32(lengthBytes, static_cast<std::uint32_t>(payload.size()));
    stream.write(reinterpret_cast<const char*>(lengthBytes.data()), 4);
    stream.write(type, 4);
    if (!payload.empty()) {
        stream.write(reinterpret_cast<const char*>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
    }

    std::vector<std::uint8_t> crcInput(4 + payload.size());
    std::copy(type, type + 4, crcInput.begin());
    std::copy(payload.begin(), payload.end(), crcInput.begin() + 4);
    std::vector<std::uint8_t> crcBytes;
    AppendBigEndian32(crcBytes, CRC32(crcInput.data(), crcInput.size()));
    stream.write(reinterpret_cast<const char*>(crcBytes.data()), 4);
    return stream.good();
}

// Minimal lossless 16-bit RGB PNG writer. The zlib stream uses uncompressed
// DEFLATE blocks; LUTs are small and numerical precision matters more than size.
bool WriteLinearPNG16(const std::filesystem::path& path, int width, int height,
                      const std::vector<float>& data) {
    std::vector<std::uint8_t> scanlines;
    scanlines.reserve(static_cast<std::size_t>(height * (1 + width * 6)));
    for (int y = 0; y < height; ++y) {
        scanlines.push_back(0); // PNG filter type: None.
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>((y * width + x) * 3);
            for (int channel = 0; channel < 3; ++channel) {
                const float value = std::clamp(data[offset + channel], 0.0f, 1.0f);
                const auto encoded = static_cast<std::uint16_t>(std::lround(value * 65535.0f));
                scanlines.push_back(static_cast<std::uint8_t>(encoded >> 8u));
                scanlines.push_back(static_cast<std::uint8_t>(encoded & 0xFFu));
            }
        }
    }

    std::vector<std::uint8_t> zlib;
    zlib.reserve(scanlines.size() + scanlines.size() / 65535u * 5u + 16u);
    zlib.push_back(0x78); // Deflate, 32K window.
    zlib.push_back(0x01); // Fastest/no compression, valid FCHECK.
    std::size_t offset = 0;
    while (offset < scanlines.size()) {
        const std::size_t remaining = scanlines.size() - offset;
        const std::uint16_t blockSize = static_cast<std::uint16_t>(
            std::min<std::size_t>(remaining, 65535u));
        const bool isFinal = offset + blockSize == scanlines.size();
        zlib.push_back(isFinal ? 0x01 : 0x00); // BFINAL + stored BTYPE.
        zlib.push_back(static_cast<std::uint8_t>(blockSize & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>(blockSize >> 8u));
        const std::uint16_t inverseSize = static_cast<std::uint16_t>(~blockSize);
        zlib.push_back(static_cast<std::uint8_t>(inverseSize & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>(inverseSize >> 8u));
        zlib.insert(zlib.end(), scanlines.begin() + static_cast<std::ptrdiff_t>(offset),
                    scanlines.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }
    AppendBigEndian32(zlib, Adler32(scanlines));

    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    stream.write(reinterpret_cast<const char*>(signature), 8);

    std::vector<std::uint8_t> ihdr;
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(width));
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(16); // Bit depth.
    ihdr.push_back(2);  // Truecolor RGB.
    ihdr.push_back(0);  // Compression method.
    ihdr.push_back(0);  // Filter method.
    ihdr.push_back(0);  // No interlace.

    return WritePNGChunk(stream, "IHDR", ihdr) &&
           WritePNGChunk(stream, "IDAT", zlib) &&
           WritePNGChunk(stream, "IEND", {});
}

// PFM is kept as an unquantized, easy-to-parse reference output. Rows are
// written bottom-to-top as required by the PFM convention.
bool WritePFM(const std::filesystem::path& path, int width, int height,
              const std::vector<float>& data) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "PF\n" << width << ' ' << height << "\n-1.0\n";
    for (int y = height - 1; y >= 0; --y) {
        const float* row = data.data() + static_cast<std::size_t>(y * width * 3);
        stream.write(reinterpret_cast<const char*>(row),
                     static_cast<std::streamsize>(width * 3 * sizeof(float)));
    }
    return stream.good();
}

void WriteMetadata(const std::filesystem::path& path, const Options& options,
                   float minimumE, float maximumE, float minimumAverage, float maximumAverage) {
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("Could not write metadata file: " + path.string());
    }
    stream << std::fixed << std::setprecision(8)
           << "{\n"
           << "  \"generator\": \"KullaContyBRDF LUT generator v3\",\n"
           << "  \"resolution\": " << options.size << ",\n"
           << "  \"samples_per_texel\": " << options.sampleCount << ",\n"
           << "  \"parameterization\": {\"x\": \"NdotX\", \"y\": \"perceptual_roughness\"},\n"
           << "  \"microfacet_distribution\": \"isotropic GGX\",\n"
           << "  \"geometry\": \"exact separable Smith GGX\",\n"
           << "  \"E_mu_range\": [" << minimumE << ", " << maximumE << "],\n"
           << "  \"E_avg_range\": [" << minimumAverage << ", " << maximumAverage << "],\n"
           << "  \"primary_outputs\": [\"E_mu.png\", \"E_avg.png\", \"BRDF_SplitSum.png\"],\n"
           << "  \"reference_outputs\": [\"E_mu.pfm\", \"E_avg.pfm\", \"BRDF_SplitSum.pfm\"],\n"
           << "  \"validation\": \"validation.json\"\n"
           << "}\n";
}

ValidationMetrics ValidateLUTs(const Options& options,
                               const std::vector<float>& directionalAlbedo,
                               const std::vector<float>& averageAlbedo,
                               const std::vector<float>& splitSum) {
    constexpr float gaussMu[4] = {
        0.0694318442f, 0.3300094782f, 0.6699905218f, 0.9305681558f};
    constexpr float gaussWeight[4] = {
        0.1739274226f, 0.3260725774f, 0.3260725774f, 0.1739274226f};
    constexpr float testF0[3] = {0.04f, 0.5f, 0.9f};
    // The Kulla-Conty denominator becomes numerically singular as E_avg -> 1.
    // Validate the operational domain used by this UE project, whose renderer
    // clamps minimum perceptual roughness to 0.02.
    constexpr double minimumValidationRoughness = 0.02;
    constexpr double minimumValidationNoV = 0.02;
    // The max threshold accounts for finite-precision E_avg storage close to
    // the smooth-limit singularity; the RMS error remains much lower.
    constexpr double whiteFurnaceTolerance = 5.0e-4;
    constexpr double gauss4Tolerance = 1.0e-3;
    constexpr double coloredEnergyTolerance = 1.0e-3;

    ValidationMetrics metrics;
    double whiteSquaredError = 0.0;
    double gaussSquaredError = 0.0;
    std::size_t whiteSampleCount = 0;

    for (int y = 0; y < options.size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(options.size);
        const std::size_t rowOffset = static_cast<std::size_t>(y * options.size * 3);
        const double eAverage = averageAlbedo[rowOffset];

        double missingEnergyIntegral = 0.0;
        for (int lightIndex = 0; lightIndex < options.size; ++lightIndex) {
            const double noL = (static_cast<double>(lightIndex) + 0.5) /
                               static_cast<double>(options.size);
            const std::size_t offset = rowOffset + static_cast<std::size_t>(lightIndex * 3);
            missingEnergyIntegral += (1.0 - directionalAlbedo[offset]) * noL;
        }
        missingEnergyIntegral *= 2.0 / static_cast<double>(options.size);

        double gaussAverage = 0.0;
        for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex) {
            const IntegratedBRDF integrated =
                IntegrateBRDF(gaussMu[sampleIndex], roughness, options.sampleCount);
            gaussAverage += 2.0 * gaussWeight[sampleIndex] *
                            integrated.directionalAlbedo * gaussMu[sampleIndex];
        }
        const double gaussError = std::abs(gaussAverage - eAverage);
        metrics.gauss4AverageMaxError = std::max(metrics.gauss4AverageMaxError, gaussError);
        gaussSquaredError += gaussError * gaussError;

        for (int viewIndex = 0; viewIndex < options.size; ++viewIndex) {
            const std::size_t offset = rowOffset + static_cast<std::size_t>(viewIndex * 3);
            const double eNoV = directionalAlbedo[offset];
            const double missingAverage = 1.0 - eAverage;
            const double multipleScatterAlbedo = missingAverage > 1.0e-7
                ? (1.0 - eNoV) * missingEnergyIntegral / missingAverage
                : 0.0;
            const double noV = (static_cast<double>(viewIndex) + 0.5) /
                               static_cast<double>(options.size);
            if (roughness >= minimumValidationRoughness && noV >= minimumValidationNoV) {
                const double whiteFurnaceEnergy = eNoV + multipleScatterAlbedo;
                const double whiteError = std::abs(whiteFurnaceEnergy - 1.0);
                metrics.whiteFurnaceMaxError =
                    std::max(metrics.whiteFurnaceMaxError, whiteError);
                whiteSquaredError += whiteError * whiteError;
                ++whiteSampleCount;
            }

            const double splitA = splitSum[offset];
            const double splitB = splitSum[offset + 1];
            for (const float f0 : testF0) {
                const double fAverage = f0 + (1.0 - f0) / 21.0;
                const double fMultipleScatter =
                    fAverage * eAverage /
                    std::max(1.0 - fAverage * (1.0 - eAverage), 1.0e-7);
                const double singleScatterAlbedo = splitA * f0 + splitB * (1.0 - f0);
                const double combinedAlbedo =
                    singleScatterAlbedo + fMultipleScatter * (1.0 - eNoV);
                metrics.maximumColoredDirectionalAlbedo =
                    std::max(metrics.maximumColoredDirectionalAlbedo, combinedAlbedo);
            }
        }
    }

    metrics.whiteFurnaceRmsError = whiteSampleCount > 0
        ? std::sqrt(whiteSquaredError / static_cast<double>(whiteSampleCount))
        : 0.0;
    metrics.gauss4AverageRmsError =
        std::sqrt(gaussSquaredError / static_cast<double>(options.size));
    metrics.passed =
        metrics.whiteFurnaceMaxError <= whiteFurnaceTolerance &&
        metrics.gauss4AverageMaxError <= gauss4Tolerance &&
        metrics.maximumColoredDirectionalAlbedo <= 1.0 + coloredEnergyTolerance;
    return metrics;
}

void WriteValidation(const std::filesystem::path& path,
                     const Options& options,
                     const ValidationMetrics& metrics) {
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("Could not write validation file: " + path.string());
    }
    stream << std::fixed << std::setprecision(10)
           << "{\n"
           << "  \"resolution\": " << options.size << ",\n"
           << "  \"samples_per_texel\": " << options.sampleCount << ",\n"
           << "  \"white_furnace_max_abs_error\": " << metrics.whiteFurnaceMaxError << ",\n"
           << "  \"white_furnace_rms_error\": " << metrics.whiteFurnaceRmsError << ",\n"
           << "  \"gauss4_eavg_max_abs_error\": " << metrics.gauss4AverageMaxError << ",\n"
           << "  \"gauss4_eavg_rms_error\": " << metrics.gauss4AverageRmsError << ",\n"
           << "  \"maximum_colored_directional_albedo\": "
           << metrics.maximumColoredDirectionalAlbedo << ",\n"
           << "  \"white_furnace_domain\": {\"minimum_roughness\": 0.02, \"minimum_NdotV\": 0.02},\n"
           << "  \"tolerances\": {\"white_furnace\": 0.0005, \"gauss4_eavg\": 0.001, \"colored_energy\": 0.001},\n"
           << "  \"passed\": " << (metrics.passed ? "true" : "false") << "\n"
           << "}\n";
}

int ParsePositiveInteger(const std::string& value, const char* optionName) {
    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0) {
            throw std::invalid_argument("not positive");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(optionName) + " expects a positive integer");
    }
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        const std::string argument = argv[argumentIndex];
        if (argument == "--size" && argumentIndex + 1 < argc) {
            options.size = ParsePositiveInteger(argv[++argumentIndex], "--size");
        } else if (argument == "--samples" && argumentIndex + 1 < argc) {
            options.sampleCount = ParsePositiveInteger(argv[++argumentIndex], "--samples");
        } else if (argument == "--output" && argumentIndex + 1 < argc) {
            options.outputDirectory = argv[++argumentIndex];
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: LUT [--size 256] [--samples 1024] [--output Generated]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown or incomplete argument: " + argument);
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = ParseOptions(argc, argv);
        std::filesystem::create_directories(options.outputDirectory);

        const std::size_t texelCount = static_cast<std::size_t>(options.size * options.size);
        std::vector<float> splitSum(texelCount * 3, 0.0f);
        std::vector<float> directionalAlbedo(texelCount * 3, 0.0f);
        std::vector<float> averageAlbedo(texelCount * 3, 0.0f);
        std::vector<float> averages(static_cast<std::size_t>(options.size), 0.0f);

        float minimumE = 1.0f;
        float maximumE = 0.0f;
        for (int y = 0; y < options.size; ++y) {
            const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(options.size);
            float weightedEnergySum = 0.0f;

            for (int x = 0; x < options.size; ++x) {
                const float noV = (static_cast<float>(x) + 0.5f) / static_cast<float>(options.size);
                const IntegratedBRDF integrated = IntegrateBRDF(noV, roughness, options.sampleCount);
                const float energy = std::clamp(integrated.directionalAlbedo, 0.0f, 1.0f);

                SetRGB(splitSum, options.size, x, y, integrated.splitA, integrated.splitB, 0.0f);
                SetRGB(directionalAlbedo, options.size, x, y, energy, energy, energy);
                weightedEnergySum += energy * noV;
                minimumE = std::min(minimumE, energy);
                maximumE = std::max(maximumE, energy);
            }

            // Midpoint quadrature for E_avg = 2 * integral_0^1 E(mu) * mu dmu.
            averages[static_cast<std::size_t>(y)] =
                std::clamp(2.0f * weightedEnergySum / static_cast<float>(options.size), 0.0f, 1.0f);
        }

        float minimumAverage = 1.0f;
        float maximumAverage = 0.0f;
        for (int y = 0; y < options.size; ++y) {
            const float average = averages[static_cast<std::size_t>(y)];
            minimumAverage = std::min(minimumAverage, average);
            maximumAverage = std::max(maximumAverage, average);
            for (int x = 0; x < options.size; ++x) {
                SetRGB(averageAlbedo, options.size, x, y, average, average, average);
            }
        }

        const auto WriteAllFormats = [&](const char* baseName, const std::vector<float>& data) {
            const auto png16Path = options.outputDirectory / (std::string(baseName) + ".png");
            const auto hdrPath = options.outputDirectory / (std::string(baseName) + ".hdr");
            const auto pfmPath = options.outputDirectory / (std::string(baseName) + ".pfm");
            const auto previewPath = options.outputDirectory / (std::string(baseName) + "_preview.png");
            if (!WriteLinearPNG16(png16Path, options.size, options.size, data) ||
                !WriteHDR(hdrPath, options.size, options.size, data) ||
                !WritePFM(pfmPath, options.size, options.size, data) ||
                !WritePreviewPNG(previewPath, options.size, options.size, data)) {
                throw std::runtime_error("Failed to write one or more outputs for " + std::string(baseName));
            }
        };

        WriteAllFormats("E_mu", directionalAlbedo);
        WriteAllFormats("E_avg", averageAlbedo);
        WriteAllFormats("BRDF_SplitSum", splitSum);
        const ValidationMetrics validation =
            ValidateLUTs(options, directionalAlbedo, averageAlbedo, splitSum);
        WriteValidation(options.outputDirectory / "validation.json", options, validation);
        WriteMetadata(options.outputDirectory / "metadata.json", options,
                      minimumE, maximumE, minimumAverage, maximumAverage);

        if (!validation.passed) {
            throw std::runtime_error(
                "LUT validation failed; inspect validation.json for numerical errors");
        }

        std::cout << "Generated Kulla-Conty LUTs in " << options.outputDirectory << '\n'
                  << "E(mu) range: [" << minimumE << ", " << maximumE << "]\n"
                  << "E(avg) range: [" << minimumAverage << ", " << maximumAverage << "]\n"
                  << "White-furnace max error: " << validation.whiteFurnaceMaxError << '\n'
                  << "Gauss-4 E(avg) max error: " << validation.gauss4AverageMaxError << '\n'
                  << "Maximum colored directional albedo: "
                  << validation.maximumColoredDirectionalAlbedo << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
