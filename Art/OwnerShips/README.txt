PirateSeas - modele de vapoare (generate in Blender 5.2)

FBX/              7 vapoare, cate un FBX fiecare (texturi incorporate)
textures/         texturile PNG (lemn, panza, steaguri)
PirateSeas_Ships.blend   scena cu toata flota
ships_lib1.py / ships_lib2.py   generatorul (se poate rula din nou in Blender)

Vapoare: Sloop (16 m), Schooner (22 m), Brig (28 m), Merchantman (32 m),
         Frigate (42 m), Galleon (44 m), ManOWar (56 m, 3 punti de tunuri)

Import in Unity
- Scara 1 unitate = 1 m. Prova e pe +Z, sus e +Y (exportat cu Apply Transform).
- Model tab: Import BlendShapes = ON.  Animation tab: Loop Time = ON (clip 240 cadre / 10 s la 24 fps).
- Materials tab: Extract Textures / Extract Materials daca vrei sa le editezi.

Ierarhie
Ship_X                  radacina statica (pune aici Rigidbody / scripturi)
  Ship_X_Rock           legana vaporul (animatie: ruliu, tangaj, hula)
    _Hull, _Deck, _DeckProps, _Masts, _Yards, _Rigging
    _Rudder             pivot la balama; roteste pe axa verticala
    _Wheel              pivot la butuc; roteste pe axa longitudinala
    _Cannons_Port_N / _Cannons_Starboard_N   tunurile pe fiecare parte si nivel
    Muzzle_Port_XYY / Muzzle_Starboard_XYY   puncte goale la gura fiecarui tun (spawn ghiulele / fum)
    _Sail_...           fiecare panza separata, blendshapes: Billow (umflata), Furled (stransa)
    _Flag               blendshapes Wave_0/90/180/270 (animatie de fluturat)

Panze din cod:  skinnedMeshRenderer.SetBlendShapeWeight(index "Furled", 0..100)
