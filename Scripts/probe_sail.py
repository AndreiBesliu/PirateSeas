"""One-variable probe: turn the sail's normal map off and nothing else.

The ring on every sail survived three guesses - biplanar seam, shadow bias,
mast shadow. Each guess cost a run. This changes ONE parameter on ONE instance
so the next capture answers a yes/no question instead of a vague one."""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

mi = EAL.load_asset("/Game/Materials/MI_Sail")
v = float(os.environ.get("PS_SAIL_NORMAL", "0.0"))
MEL.set_material_instance_scalar_parameter_value(mi, "NormalStrength", v)
EAL.save_asset("/Game/Materials/MI_Sail")
got = MEL.get_material_instance_scalar_parameter_value(mi, "NormalStrength")
unreal.log("PROBE MI_Sail NormalStrength set=%.2f readback=%.2f" % (v, got))
