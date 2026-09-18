"""Translucency pass and every related knob, on both materials."""
import unreal

EAL = unreal.EditorAssetLibrary


def L(m):
    unreal.log("TRAILPROBE " + m)


L("MaterialTranslucencyPass values: %s"
  % [v for v in dir(unreal.MaterialTranslucencyPass) if not v.startswith("_")])

for path in ("/Game/Materials/M_GunSmoke", "/Game/Materials/M_ShotTrail"):
    m = EAL.load_asset(path)
    bits = []
    for prop in ("translucency_pass", "allow_translucent_custom_depth_writes",
                 "translucency_lighting_mode", "disable_depth_test",
                 "translucency_directional_lighting_intensity",
                 "num_customized_u_vs", "is_sky", "compute_fog_per_pixel",
                 "output_translucent_velocity", "render_after_dof",
                 "enable_separate_translucency", "use_translucency_vertex_fog",
                 "apply_cloud_fogging", "allow_front_layer_translucency"):
        try:
            bits.append("%s=%s" % (prop, m.get_editor_property(prop)))
        except Exception:
            pass
    L("%s: %s" % (m.get_name(), " ".join(bits)))
