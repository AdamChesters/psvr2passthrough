# assets/

No model files are required by the hybrid beta. Camera frames come from
PSVR2Toolkit through SteamVR's tracked-camera API. The layer no longer reads
Sony's camera shared-memory interface directly.

The required `openvr_api.dll` is packaged beside the layer DLL, not in this
folder. See [the beta guide](../docs/HYBRID-BETA.md) for runtime requirements.
