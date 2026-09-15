# Ce a mai raportat recenzia a doua şi NU e reparat încă

A doua pasă adversarială (12 lentile-agent + verificatori, 15.09.2026) a raportat
34 de constatări. Douăsprezece au fost verificate adversarial — toate au
supravieţuit — şi toate douăsprezece sunt reparate în commit-urile din ziua asta.

Restul de douăzeci şi două au fost raportate dar **NU verificate** (bugetul de
verificare a fost primele două pe lentilă, după gravitate). Lista de mai jos e
ce a rămas, ca să nu dispară tăcut. Fiecare are fişierul şi linia din momentul
raportării.

Ordinea nu e o prioritizare a mea — e ordinea în care le-a dat recenzia.

## Deja reparate din lista neverificată

- `Scripts/ship_materials.py:497` — MI_Foliage pe un singur plan (un sfert din
  fiecare plantă dintr-o singură coloană de texeli). **REPARAT**: trei plane.
- `Scripts/ship_materials.py:326` — pe meshuri instanţiate „Local" e cadrul
  COMPONENTEI, nu al instanţei. **REPARAT**: spaţiu de instanţă peste tot.
- `Scripts/island_material.py:207` — roca insulei eşantionată pe (x, z) şi
  aplicată în cadrul tangent al unui layout (x, y). **REPARAT**: gradienţi.
- `tools/ci_measure.py:73` — un singur log partajat, fără dovadă că vine din
  rularea tocmai lansată. **REPARAT**: logul se şterge înainte şi linia de
  comandă din el se verifică flag cu flag.
- `tools/ci_measure.py:89` — `groundings` număra LINII de log, două pe lovitură.
  **REPARAT**: numără loviturile; linia de bază a scăzut de la 2 la 1.
- `README.md:468` — tabelul zicea „exact o insulă, oricât ai cere"; codul face
  până la opt. **REPARAT**.

## Rămase

- `Scripts/sea_material.py:639` — `ScatterRangeCm` e o constantă de 150 cm în
  material, pe care C++ n-o împinge niciodată: exact defectul pe care l-a reparat
  `FoamStartCm`, rămas într-un alt colţ al aceleiaşi foi.
- `Source/PirateSeas/OceanSurface.h:179` — trei butoane `EditAnywhere` de siaj pe
  care nu le citeşte nimeni şi nu le împinge nicăieri: le poţi roti fără să se
  schimbe un pixel.
- `Source/PirateSeas/OceanSurface.cpp:197` — `PushIslands()` îşi pune zăvorul
  `bIslandsPushed` chiar şi când n-a găsit nicio insulă, deci e o singură
  încercare, deşi antetul îl descrie ca amânat până apar insulele.
- `Source/PirateSeas/Island.cpp:112` — linia ţărmului se scalează cu mărimea
  insulei, dar `M_Island` citeşte Z absolut din lume: pe orice insulă în afară de
  cea autorizată, plantele şi vopseaua se despart.
- `Source/PirateSeas/OceanSurface.cpp:487` — linia WAKELOG tipărea două
  măsurători diferite sub aceeaşi cheie `live=`. **Parţial reparat** azi (a doua
  a devenit `alive=`), dar linia tot poartă o constantă de compilare (`of 24`)
  deghizată în măsurătoare.
- `Source/PirateSeas/ShipPawn.cpp:676` — rezolvarea de anticipaţie scade mereu
  viteza proprie a navei, chiar şi când `-ShipInheritVel=0` înseamnă că ghiuleaua
  n-a purtat-o niciodată.
- `Source/PirateSeas/CannonBall.cpp:258` — o ghiulea fără ţintă, sau a cărei
  ţintă s-a scufundat în zbor, se înregistrează ca lovitură în plin
  (`along=+0.0 lateral=+0.0`), deci mediile de precizie o numără ca perfectă.
- `.github/workflows/checks.yml:38` — controlul negativ dă vina pe gardă când de
  fapt stricăciunea deliberată e cea care n-a fost aplicată.
- `.github/workflows/measure.yml:36` — şterge 2,9 GB de build înainte de fiecare
  build, deci prima rulare e o recompilare la rece într-un timeout de 45 min.

## Cum se citeşte lista asta

„Neverificat" nu înseamnă „fals". Înseamnă că nimeni n-a încercat încă s-o
combată — iar în pasa asta, din douăsprezece constatări pe care CINEVA a încercat
serios să le combată, au căzut zero. Rata aia e un motiv să le iei în serios, nu
un motiv să le crezi pe cuvânt.
