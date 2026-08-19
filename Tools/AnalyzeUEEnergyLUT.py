"""Analyze UE 5.8's exported GGX energy LUT and rank E_avg fast paths.

The EXR reader intentionally supports the small subset emitted by UE's texture
exporter: scanline, HALF channels, ZIP compression, increasing line order.
No third-party EXR package is required.
"""

from __future__ import annotations

import json
import hashlib
import math
import re
import struct
import sys
import zlib
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "Intermediate" / "KullaConty" / "UE58_GGX_ReflectionEnergy.exr"
DEFAULT_OUTPUT = ROOT / "Intermediate" / "KullaConty" / "ue_lut_analysis.json"
SHADER_PATH = ROOT / "EnginePatch" / "UE5.8.1" / "KullaContyEAvg.ush"


def _cstring(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(0, offset)
    return data[offset:end].decode("ascii"), end + 1


def _undo_zip_filter(encoded: bytes) -> bytes:
    predicted = bytearray(zlib.decompress(encoded))
    for index in range(1, len(predicted)):
        predicted[index] = (predicted[index - 1] + predicted[index] - 128) & 0xFF

    decoded = bytearray(len(predicted))
    split = (len(predicted) + 1) // 2
    decoded[0::2] = predicted[:split]
    decoded[1::2] = predicted[split:]
    return bytes(decoded)


def read_ue_half_zip_exr(path: Path) -> tuple[np.ndarray, list[str]]:
    data = path.read_bytes()
    if data[:4] != struct.pack("<I", 20000630):
        raise ValueError(f"Not an OpenEXR file: {path}")
    if struct.unpack_from("<I", data, 4)[0] & 0x00000200:
        raise ValueError("Multipart EXR is not supported")

    offset = 8
    attributes: dict[str, tuple[str, bytes]] = {}
    while data[offset] != 0:
        name, offset = _cstring(data, offset)
        attribute_type, offset = _cstring(data, offset)
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        attributes[name] = (attribute_type, data[offset : offset + size])
        offset += size
    offset += 1

    if attributes["compression"][1] != b"\x03":
        raise ValueError("Expected ZIP-compressed scanline EXR")
    if attributes["lineOrder"][1] != b"\x00":
        raise ValueError("Expected increasing EXR line order")

    minimum_x, minimum_y, maximum_x, maximum_y = struct.unpack(
        "<4i", attributes["dataWindow"][1]
    )
    width = maximum_x - minimum_x + 1
    height = maximum_y - minimum_y + 1

    channels: list[str] = []
    channel_data = attributes["channels"][1]
    channel_offset = 0
    while channel_data[channel_offset] != 0:
        channel_name, channel_offset = _cstring(channel_data, channel_offset)
        pixel_type = struct.unpack_from("<i", channel_data, channel_offset)[0]
        x_sampling, y_sampling = struct.unpack_from(
            "<ii", channel_data, channel_offset + 8
        )
        channel_offset += 16
        if pixel_type != 1 or x_sampling != 1 or y_sampling != 1:
            raise ValueError("Expected unsampled HALF channels")
        channels.append(channel_name)

    lines_per_chunk = 16
    chunk_count = math.ceil(height / lines_per_chunk)
    chunk_offsets = struct.unpack_from(f"<{chunk_count}Q", data, offset)
    image = np.zeros((height, width, len(channels)), dtype=np.float32)

    for chunk_file_offset in chunk_offsets:
        first_y, packed_size = struct.unpack_from("<ii", data, chunk_file_offset)
        packed_start = chunk_file_offset + 8
        raw = _undo_zip_filter(data[packed_start : packed_start + packed_size])
        line_count = min(lines_per_chunk, maximum_y - first_y + 1)
        expected_size = line_count * width * len(channels) * 2
        if len(raw) != expected_size:
            raise ValueError(
                f"Unexpected EXR chunk size: decoded {len(raw)}, expected {expected_size}"
            )

        raw_offset = 0
        for y in range(first_y, first_y + line_count):
            target_y = y - minimum_y
            for channel_index in range(len(channels)):
                byte_count = width * 2
                image[target_y, :, channel_index] = np.frombuffer(
                    raw[raw_offset : raw_offset + byte_count], dtype="<f2"
                ).astype(np.float32)
                raw_offset += byte_count

    return image, channels


def bilinear_sample(channel: np.ndarray, no_v: np.ndarray, roughness: np.ndarray) -> np.ndarray:
    height, width = channel.shape
    x = np.asarray(no_v) * width - 0.5
    y = np.asarray(roughness) * height - 0.5
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    tx = x - x0
    ty = y - y0
    x1 = np.clip(x0 + 1, 0, width - 1)
    y1 = np.clip(y0 + 1, 0, height - 1)
    x0 = np.clip(x0, 0, width - 1)
    y0 = np.clip(y0, 0, height - 1)
    return (
        (1.0 - tx) * (1.0 - ty) * channel[y0, x0]
        + tx * (1.0 - ty) * channel[y0, x1]
        + (1.0 - tx) * ty * channel[y1, x0]
        + tx * ty * channel[y1, x1]
    )


def average_energy(
    energy: np.ndarray, roughness: np.ndarray, nodes: np.ndarray, weights: np.ndarray
) -> np.ndarray:
    roughness_grid = np.asarray(roughness)[:, None]
    node_grid = np.broadcast_to(nodes[None, :], (len(roughness), len(nodes)))
    sampled = bilinear_sample(energy, node_grid, roughness_grid)
    return np.sum(2.0 * weights[None, :] * nodes[None, :] * sampled, axis=1)


def error_metrics(reference: np.ndarray, candidate: np.ndarray) -> dict[str, float]:
    error = candidate - reference
    return {
        "max_abs_error": float(np.max(np.abs(error))),
        "rms_error": float(np.sqrt(np.mean(error * error))),
    }


def linear_lut_sample(values: np.ndarray, coordinate: np.ndarray) -> np.ndarray:
    position = np.asarray(coordinate) * len(values) - 0.5
    lower_unclamped = np.floor(position).astype(np.int32)
    blend = position - lower_unclamped
    lower = np.clip(lower_unclamped, 0, len(values) - 1)
    upper = np.clip(lower_unclamped + 1, 0, len(values) - 1)
    return (1.0 - blend) * values[lower] + blend * values[upper]


def read_shader_eavg_constants(path: Path) -> np.ndarray:
    shader = path.read_text(encoding="utf-8")
    match = re.search(
        r"KCEAverageDirectionalAlbedoLUT\[32\]\s*=\s*\{(?P<body>.*?)\};",
        shader,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"Could not find KCEAverageDirectionalAlbedoLUT in {path}")
    values = [
        float(value)
        for value in re.findall(
            r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?",
            match.group("body").replace("f", ""),
        )
    ]
    if len(values) != 32:
        raise ValueError(f"Expected 32 shader E_avg constants, found {len(values)}")
    return np.asarray(values, dtype=np.float32)


def white_furnace_max_error(
    energy: np.ndarray,
    roughness: np.ndarray,
    reference_average: np.ndarray,
    candidate_average: np.ndarray,
) -> float:
    no_v = np.linspace(0.02, 1.0, 257)
    sampled = bilinear_sample(
        energy,
        np.broadcast_to(no_v[None, :], (len(roughness), len(no_v))),
        np.broadcast_to(roughness[:, None], (len(roughness), len(no_v))),
    )
    actual_missing = 1.0 - reference_average[:, None]
    candidate_missing = np.maximum(1.0 - candidate_average[:, None], 1.0e-7)
    furnace = sampled + (1.0 - sampled) * actual_missing / candidate_missing
    return float(np.max(np.abs(furnace - 1.0)))


def main() -> None:
    input_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_INPUT
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUTPUT
    image, channels = read_ue_half_zip_exr(input_path)
    if "R" not in channels:
        raise ValueError(f"EXR has no R channel: {channels}")
    energy = image[:, :, channels.index("R")]

    roughness = np.linspace(0.02, 1.0, 2049)
    reference_nodes, reference_weights = np.polynomial.legendre.leggauss(128)
    reference_nodes = 0.5 * (reference_nodes + 1.0)
    reference_weights = 0.5 * reference_weights
    reference = average_energy(energy, roughness, reference_nodes, reference_weights)
    row_roughness = (np.arange(energy.shape[0], dtype=np.float64) + 0.5) / energy.shape[0]
    row_average = average_energy(
        energy, row_roughness, reference_nodes, reference_weights
    )
    shader_row_average = read_shader_eavg_constants(SHADER_PATH)

    gauss4_nodes = np.array(
        [0.0694318442, 0.3300094782, 0.6699905218, 0.9305681558]
    )
    gauss4_weights = np.array(
        [0.1739274226, 0.3260725774, 0.3260725774, 0.1739274226]
    )
    gauss2_nodes = np.array([0.2113248654, 0.7886751346])
    gauss2_weights = np.array([0.5, 0.5])

    candidates: dict[str, dict[str, object]] = {}
    for name, candidate in {
        "gauss4": average_energy(energy, roughness, gauss4_nodes, gauss4_weights),
        "gauss2": average_energy(energy, roughness, gauss2_nodes, gauss2_weights),
        "single_mu_2_over_3": bilinear_sample(
            energy, np.full_like(roughness, 2.0 / 3.0), roughness
        ),
        "shader_embedded_32_linear_float32": linear_lut_sample(
            shader_row_average, roughness
        ),
    }.items():
        candidates[name] = error_metrics(reference, candidate)
        candidates[name]["white_furnace_max_abs_error"] = white_furnace_max_error(
            energy, roughness, reference, candidate
        )

    missing = 1.0 - reference
    fitted_candidates: list[dict[str, object]] = []
    for leading_power in range(1, 7):
        scaled_missing = missing / np.power(roughness, leading_power)
        for degree in range(2, 9):
            coefficients = np.polynomial.polynomial.polyfit(
                roughness, scaled_missing, degree
            )
            predicted_missing = np.power(roughness, leading_power) * np.polynomial.polynomial.polyval(
                roughness, coefficients
            )
            predicted = 1.0 - np.clip(predicted_missing, 0.0, 1.0)
            metrics = error_metrics(reference, predicted)
            metrics["white_furnace_max_abs_error"] = white_furnace_max_error(
                energy, roughness, reference, predicted
            )
            metrics.update(
                {
                    "leading_power": leading_power,
                    "polynomial_degree": degree,
                    "coefficients_low_to_high": [float(value) for value in coefficients],
                }
            )
            fitted_candidates.append(metrics)

    fitted_candidates.sort(
        key=lambda item: (
            item["white_furnace_max_abs_error"],
            item["max_abs_error"],
            item["polynomial_degree"],
        )
    )
    report = {
        "source": str(input_path),
        "source_sha256": hashlib.sha256(input_path.read_bytes()).hexdigest(),
        "resolution": [int(energy.shape[1]), int(energy.shape[0])],
        "channels": channels,
        "channel_ranges": {
            channel: [
                float(np.min(image[:, :, channel_index])),
                float(np.max(image[:, :, channel_index])),
            ]
            for channel_index, channel in enumerate(channels)
        },
        "energy_range": [float(np.min(energy)), float(np.max(energy))],
        "analysis_domain": {"minimum_roughness": 0.02, "maximum_roughness": 1.0},
        "reference": "128-point Gauss-Legendre integration of UE bilinear LUT",
        "shader": str(SHADER_PATH),
        "shader_constant_max_abs_error": float(
            np.max(np.abs(shader_row_average.astype(np.float64) - row_average))
        ),
        "sampled_candidates": candidates,
        "embedded_32_eavg_low_to_high_roughness": [
            float(value) for value in row_average
        ],
        "best_polynomial_candidates": fitted_candidates[:12],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
