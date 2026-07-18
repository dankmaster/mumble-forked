# DeepFilterNet Runtime Assets

Place DeepFilterNet ONNX model archives (`*.tar.gz`) in this directory to have them copied next to the client binary
when `-Ddeepfilternet=ON` is enabled.

The product input-enhancement recipes (Quality, Voice Focus and Auto Quality) use the low-latency model. The standard
model remains packaged as a benchmark challenger, but no current product recipe authorizes it:

- `DeepFilterNet3_onnx.tar.gz`
- `DeepFilterNet3_ll_onnx.tar.gz`
