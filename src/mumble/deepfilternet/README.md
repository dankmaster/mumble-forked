# DeepFilterNet Runtime Assets

Place DeepFilterNet ONNX model archives (`*.tar.gz`) in this directory to have them copied next to the client binary
when `-Ddeepfilternet=ON` is enabled.

The runtime currently uses the standard model for the original DeepFilterNet profiles and the low-latency model only
for the explicit low-latency profile:

- `DeepFilterNet3_onnx.tar.gz`
- `DeepFilterNet3_ll_onnx.tar.gz`
