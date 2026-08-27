"""Build-time conversion of the official DPDFNet ONNX graph to Core ML."""
from pathlib import Path

import coremltools as ct
import onnx
import torch
from onnx2torch import convert


root = Path(__file__).resolve().parents[1]
source = root / "Assets/Models/dpdfnet2_48khz_hr.onnx"
destination = root / "Assets/Models/DPDFNet48k.mlpackage"

torch_model = convert(onnx.load(source)).eval()
spec = torch.zeros((1, 1, 481, 2), dtype=torch.float32)
state = torch.zeros((56436,), dtype=torch.float32)

with torch.no_grad():
    traced = torch.jit.trace(torch_model, (spec, state), strict=False)
    reference = torch_model(spec, state)
    traced_result = traced(spec, state)
    for expected, actual in zip(reference, traced_result):
        torch.testing.assert_close(expected, actual, rtol=1e-4, atol=1e-5)

model = ct.convert(
    traced,
    convert_to="mlprogram",
    inputs=[
        ct.TensorType(name="spec", shape=spec.shape, dtype=float),
        ct.TensorType(name="state_in", shape=state.shape, dtype=float),
    ],
    outputs=[ct.TensorType(name="spec_e"), ct.TensorType(name="state_out")],
    minimum_deployment_target=ct.target.macOS13,
    compute_precision=ct.precision.FLOAT16,
)
model.author = "Jenya Audio; model by CEVA DPDFNet contributors"
model.license = "Apache-2.0"
model.short_description = "Causal 48 kHz speech enhancement and dereverberation"
model.save(destination)
print(destination)
