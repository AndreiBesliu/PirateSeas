"""
The light, and the camera's response to it.

The level had a sun, a sky, a sky light, fog and clouds - and no post process
volume at all, which means the image was being developed by whatever the
engine's defaults happen to be. That is why the first captures read as flat:
no exposure control, no filmic curve worth the name, a fog thick enough to
bleach the horizon white, and a sun high enough that the sea never gets the one
thing that most says "ocean" in a photograph - the glitter path, the broken
road of sunlight running from the horizon to the camera.

Nothing here is a texture or a mesh. It is all numbers on five actors, and it
changes the picture more than any single asset in the project.

Idempotent: finds the actors it made last time instead of stacking duplicates.
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
MAP = "/Game/Maps/L_OpenSea"


SUN_LUX = float(os.environ.get("PS_SUN_LUX", "110000"))
EV_MIN = float(os.environ.get("PS_EV_MIN", "12.5"))
EV_MAX = float(os.environ.get("PS_EV_MAX", "16.0"))
SHADOW_CM = float(os.environ.get("PS_SHADOW_CM", "60000"))
SUN_PITCH = float(os.environ.get("PS_SUN_PITCH", "-26"))
SUN_YAW = float(os.environ.get("PS_SUN_YAW", "62"))
SHADOW_BIAS = float(os.environ.get("PS_SHADOW_BIAS", "0.5"))
SHADOW_SLOPE = float(os.environ.get("PS_SHADOW_SLOPE", "0.5"))


def L(m):
    unreal.log("LIGHT " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s=%s: %s" % (n, v, str(e)[:90]))
        return False


def pp(settings, name, value):
    """A post process field and its override flag. Forgetting the flag is the
    classic silent failure here: the value is stored, the volume ignores it,
    and the only symptom is that nothing changed."""
    ok = sp(settings, "override_" + name, True)
    ok = sp(settings, name, value) and ok
    return ok


def movable(actor):
    """Mobility lives on the ROOT COMPONENT, not on the actor. Calling
    actor.set_mobility raises AttributeError - and because these scripts were
    being run with their output piped away, that exception stopped the whole
    file silently for four runs while I read the GAME log afterwards and
    concluded the level "had not changed". It had not: the script never got
    past line one of the sun."""
    rc = actor.get_editor_property("root_component")
    if rc is None:
        L("WARN %s has no root component" % actor.get_name())
        return False
    rc.set_mobility(unreal.ComponentMobility.MOVABLE)
    return True


def comp(actor, cls):
    for c in actor.get_components_by_class(cls):
        return c
    return None


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()

    actors = eas.get_all_level_actors()
    by_class = {}
    for a in actors:
        by_class.setdefault(a.get_class().get_name(), []).append(a)
    L("level has %d actors: %s" % (len(actors), sorted(by_class.keys())))

    # ------------------------------------------------------------- the sun
    for sun in by_class.get("DirectionalLight", []):
        # Low enough to lay a glitter path down the water and to rake the hull
        # so its planking has somewhere to cast a shadow. A sun overhead lights
        # everything evenly, which is the same as lighting nothing.
        movable(sun)
        sun.set_actor_rotation(unreal.Rotator(0.0, SUN_PITCH, SUN_YAW), False)
        c = comp(sun, unreal.DirectionalLightComponent)
        if not c:
            continue
        # LUX, and the units here are REAL: DefaultEngine.ini sets
        # r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange, which
        # makes every luminance in the project physical. The first pass put 7
        # here - a plausible-looking number borrowed from the non-extended
        # convention - and the scene came back black, because an exposure band
        # written in real EV100 metered a world lit like dusk. Swept, not
        # argued: PS_SUN_LUX picks the value and the capture decides.
        sp(c, "intensity", SUN_LUX)
        sp(c, "light_color", unreal.Color(255, 245, 228))
        sp(c, "temperature", 5600.0)
        sp(c, "use_temperature", True)
        # The disk's angular size drives how soft every shadow edge is; 0.545
        # degrees is the real sun, and the old 0.6 was near enough. Kept.
        sp(c, "light_source_angle", 0.545)
        sp(c, "cast_shadows", True)
        sp(c, "cast_volumetric_shadow", True)
        sp(c, "dynamic_shadow_distance_movable_light", SHADOW_CM)
        sp(c, "dynamic_shadow_cascades", 4)
        sp(c, "cascade_distribution_exponent", 2.6)
        sp(c, "shadow_bias", SHADOW_BIAS)
        sp(c, "shadow_slope_bias", SHADOW_SLOPE)
        sp(c, "atmosphere_sun_light", True)
        # Light shafts: sun low over water, through cloud, is the whole point.
        sp(c, "enable_light_shaft_occlusion", True)
        sp(c, "occlusion_mask_darkness", 0.35)
        sp(c, "enable_light_shaft_bloom", True)
        sp(c, "bloom_scale", 0.18)
        sp(c, "bloom_threshold", 0.02)
        r = sun.get_actor_rotation()
        L("sun: VERIFIED intensity=%.0f lux rot=(%.0f,%.0f) mobility=%s shadow=%.0f cm"
          % (c.get_editor_property("intensity"), r.pitch, r.yaw,
             sun.get_editor_property("root_component").get_editor_property("mobility"),
             c.get_editor_property("dynamic_shadow_distance_movable_light")))

    # -------------------------------------------------------- the sky light
    for sl in by_class.get("SkyLight", []):
        c = comp(sl, unreal.SkyLightComponent)
        if not c:
            continue
        # Captured from the real sky every frame rather than baked: the fill
        # light on the shadow side of a sail has to be the colour of the sky
        # it is actually under.
        # Movable FIRST. A sky light left on Stationary mobility contributes
        # nothing at all without a lighting build, and real-time capture on a
        # non-movable light is simply ignored - which showed up as a hull whose
        # shadow side was pure black under a bright sky. Every light in this
        # level is dynamic; there is no baked lighting anywhere in the project.
        movable(sl)
        sp(c, "real_time_capture", True)
        sp(c, "intensity", 1.0)
        sp(c, "volumetric_scattering_intensity", 1.0)
        sp(c, "cast_shadows", True)
        sp(c, "sky_distance_threshold", 300000.0)
        L("sky light: movable + real-time capture, intensity 1.0")

    # --------------------------------------------------------------- fog
    for fog in by_class.get("ExponentialHeightFog", []):
        c = comp(fog, unreal.ExponentialHeightFogComponent)
        if not c:
            continue
        # The old density bleached the horizon to white and took the sea's
        # colour with it. Thinner, and with the haze pushed into the distance
        # where haze belongs.
        sp(c, "fog_density", 0.0022)
        sp(c, "fog_height_falloff", 0.14)
        sp(c, "fog_max_opacity", 0.82)
        sp(c, "start_distance", 4000.0)
        sp(c, "fog_inscattering_luminance", unreal.LinearColor(0.32, 0.44, 0.58, 1.0))
        sp(c, "directional_inscattering_exponent", 12.0)
        sp(c, "directional_inscattering_start_distance", 6000.0)
        sp(c, "directional_inscattering_luminance", unreal.LinearColor(1.4, 1.1, 0.75, 1.0))
        # Volumetric fog is what turns light shafts from a screen effect into
        # something that sits in the world between the camera and the sail.
        sp(c, "enable_volumetric_fog", True)
        sp(c, "volumetric_fog_scattering_distribution", 0.35)
        sp(c, "volumetric_fog_albedo", unreal.Color(255, 253, 248))
        sp(c, "volumetric_fog_extinction_scale", 1.0)
        sp(c, "volumetric_fog_distance", 22000.0)
        L("fog: density 0.008 -> 0.0022, volumetric on")

    # ------------------------------------------------------------- clouds
    for cl in by_class.get("VolumetricCloud", []):
        c = comp(cl, unreal.VolumetricCloudComponent)
        if not c:
            continue
        sp(c, "layer_bottom_altitude", 4.0)      # km
        sp(c, "layer_height", 7.0)
        sp(c, "tracing_start_max_distance", 260.0)
        sp(c, "tracing_max_distance", 60.0)
        sp(c, "ground_albedo", unreal.Color(60, 78, 92))
        L("clouds: layer 4-11 km")

    # ------------------------------------------------------ sky atmosphere
    for sa in by_class.get("SkyAtmosphere", []):
        c = comp(sa, unreal.SkyAtmosphereComponent)
        if not c:
            continue
        sp(c, "multi_scattering_factor", 0.75)
        sp(c, "rayleigh_scattering_scale", 0.0331)
        sp(c, "mie_scattering_scale", 0.0035)
        sp(c, "mie_absorption_scale", 0.0004)
        L("sky atmosphere tuned")

    # ------------------------------------------------------ post process
    existing = by_class.get("PostProcessVolume", [])
    if existing:
        ppv = existing[0]
        L("re-using the post process volume")
    else:
        ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume,
                                         unreal.Vector(0, 0, 0))
        ppv.set_actor_label("GradeVolume")
        L("spawned the post process volume")
    sp(ppv, "unbound", True)
    sp(ppv, "priority", 1.0)

    # Structs come back as COPIES: every field below is written to a local and
    # the whole struct is put back at the end, or none of it takes.
    s = ppv.get_editor_property("settings")

    # Exposure. A fixed pair of stops, not an auto-exposure that pumps every
    # time a sail swings across the sun - and a run that renders a different
    # brightness each pass cannot be compared with the previous build.
    # Histogram metering fenced into a narrow band of stops, NOT manual. The
    # first attempt at this was manual with a bias of 11.6, on the assumption
    # that the bias names the EV100 to expose for; it does not, it is a gain
    # applied on top, so the frame came back clipped to pure white with only
    # the HUD - which draws after the tonemapper - still legible. The capture
    # is what said so: three different camera vantages produced PNGs of
    # identical byte length, which is not something three different pictures do.
    pp(s, "auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    pp(s, "auto_exposure_bias", 0.0)
    pp(s, "auto_exposure_min_brightness", EV_MIN)
    pp(s, "auto_exposure_max_brightness", EV_MAX)
    # Settles inside a second, so a capture at t=8 is not reading the tail of
    # an adaptation curve and two runs cannot disagree about the exposure.
    pp(s, "auto_exposure_speed_up", 20.0)
    pp(s, "auto_exposure_speed_down", 20.0)
    pp(s, "auto_exposure_apply_physical_camera_exposure", False)

    # A filmic curve with a real toe and shoulder. The default is nearly
    # linear, which is why bright water clipped to flat white.
    pp(s, "film_slope", 0.92)
    pp(s, "film_toe", 0.55)
    pp(s, "film_shoulder", 0.30)
    pp(s, "film_black_clip", 0.0)
    pp(s, "film_white_clip", 0.04)

    pp(s, "color_saturation", unreal.Vector4(1.06, 1.06, 1.10, 1.0))
    pp(s, "color_contrast", unreal.Vector4(1.05, 1.05, 1.06, 1.0))
    pp(s, "color_gain", unreal.Vector4(1.0, 1.0, 1.0, 1.0))

    pp(s, "bloom_method", unreal.BloomMethod.BM_SOG)
    pp(s, "bloom_intensity", 0.55)
    pp(s, "bloom_threshold", -1.0)

    # Lumen: reflections are most of what the sea is. Quality raised well
    # above default because a rough-water reflection that resolves in eight
    # rays is a smear, and a smear was exactly the complaint.
    pp(s, "dynamic_global_illumination_method",
       unreal.DynamicGlobalIlluminationMethod.LUMEN)
    pp(s, "reflection_method", unreal.ReflectionMethod.LUMEN)
    pp(s, "lumen_scene_lighting_quality", 2.0)
    pp(s, "lumen_scene_detail", 2.0)
    pp(s, "lumen_scene_view_distance", 60000.0)
    pp(s, "lumen_final_gather_quality", 2.0)
    pp(s, "lumen_reflection_quality", 2.0)
    pp(s, "lumen_front_layer_translucency_reflections", True)
    pp(s, "lumen_max_reflection_bounces", 2)

    pp(s, "ambient_occlusion_intensity", 0.55)
    pp(s, "ambient_occlusion_radius", 180.0)

    # A lens, not a window. Small amounts of all three; any more and it stops
    # reading as a photograph and starts reading as a filter.
    pp(s, "vignette_intensity", 0.32)
    pp(s, "scene_fringe_intensity", 1.2)
    pp(s, "chromatic_aberration_start_offset", 0.55)
    pp(s, "film_grain_intensity", 0.055)
    pp(s, "film_grain_texel_size", 1.0)

    # Motion blur off for captures: a blurred frame cannot be compared with
    # another blurred frame taken a build later.
    pp(s, "motion_blur_amount", 0.0)

    ppv.set_editor_property("settings", s)
    L("post process volume written")

    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    L("DONE level saved=%s" % ok)


main()




