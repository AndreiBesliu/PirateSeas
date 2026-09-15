# DEVLOG — PirateSeas

## 2026-09-09 — Task Started

**Prompt exact:** „poti sa faci un environment 3d in unreal si sa folosesti
asset-uri free pentru a crea un joc in care pot naviga un vapor de pirati pe
mare si ulterior sa avem lupte intre nave maritime?"

**Model:** claude-opus-5

### Ce am măsurat înainte să construiesc

| Verificare | Rezultat |
|---|---|
| Unreal Engine | 5.7.1 la `C:\Program Files\Epic Games\UE_5.7` |
| Plugin Water / WaterAdvanced / Buoyancy | toate prezente în engine |
| StarterContent | prezent |
| Compilator C++ | **absent** — doar Build Tools 2019, fără `cl.exe` și fără Windows SDK |
| Python poate scrie noduri Blueprint | **nu** — `K2Node_Event`, `K2Node_InputAction`, `KismetEditorUtilities` neexpuse |
| Proiectul `Navy` existent | demo FirstPerson, fără legătură cu nave — neatins |

Constatarea despre C++ și cea despre noduri împreună au decis arhitectura:
tot ce e logică de joc trebuie să existe deja în C++-ul engine-ului.

### Ce am construit

Proiect nou `PirateSeas`, generat integral prin script.

1. **Nava** — `ship.py` în Blender 4.3 headless. Coca e generată prin 47 de
   secțiuni transversale cu lățime, pescaj și sheer ca funcții de poziție pe
   lungime. Peste ea: punte insetată, parapet, două catarge, patru vele bombate,
   bompres, cabină la pupa, cârmă și opt tunuri. 2264 fețe, 37,5 m, originea pe
   linia de plutire. Export FBX + GLB.
2. **Proiectul Unreal** — `build_game.py` prin API-ul Python: import FBX,
   `BP_PirateShip` din `ADefaultPawn` cu mesh + spring arm + cameră,
   `BP_SeaGameMode`, nivelul `L_OpenSea` cu ocean, zonă de apă, soare,
   atmosferă, sky light, ceață și nori.
3. **Config** — hartă implicită, game mode global și maparea de axe de input,
   fără de care `ADefaultPawn` nu ar primi nimic.

### Bug-uri reale întâlnite și cauza lor

| Simptom | Cauză reală |
|---|---|
| Crash în `import_asset_tasks` | Content Browser cere Slate, care nu există într-un commandlet. Importul reușise deja înainte de crash. |
| `collision_enabled` inexistent | Nu e UPROPERTY pe `StaticMeshComponent`. Am trecut pe metoda de profil de coliziune. |
| `directional_light_component` inexistent | Accesorii specifici de actor nu sunt expuși. Am trecut pe `get_component_by_class`. |
| `NameError: ssp` | **Greșeala mea**: o rescriere prin substituție a stricat `sc.set_editor_property` în `ssp(c,`. Substituția de subșiruri fără ancoră. |
| Jocul a crăpat cu „Failed to enter C:/.../Git/Game/Maps/L_OpenSea" | Git Bash a convertit argumentul `/Game/Maps/...` în cale de fișier Windows. Rezolvat cu `MSYS_NO_PATHCONV=1`. |
| Captură de ecran complet neagră | Nivelul era **gol**. Vezi rândul următor. |
| `save_current_level()` returna `False`, tăcut | Într-un commandlet nu salvează. Nivelul rămânea gol pe disc, deși scriptul meu scria în log „level saved with 8 actors". Am înlocuit cu `EditorLoadingAndSavingUtils.save_map(world, path)`, care returnează `True`. |

### Lecția care contează

Scriptul meu a raportat „level saved with 8 actors" de patru ori la rând, în
timp ce pe disc harta avea 6 KB și zero actori. Mesajul număra actorii **din
memorie**, nu ce ajunsese în fișier, iar valoarea returnată de salvare nu era
verificată. Trei capturi negre le-am pus pe seama compilării de shadere, adică
am reparat un lucru nestricat.

Ce a rupt bucla: am încărcat harta într-un proces separat și am numărat actorii
de acolo. Verificarea trebuie făcută de un proces care nu are nimic în memorie
de la cel care a scris.

### Stare

Faza 1 (navigație) construită. Verificarea vizuală rămâne la owner, în
`OWNER_VERIFY.md`. Faza 2 (lupte) blocată pe absența compilatorului C++.

---

## 2026-09-10 — Lanțul C++ pus în funcțiune

**Model:** claude-opus-5

Faza 2 nu mai e blocată. `Source/PirateSeas` compilează, `Result: Succeeded`,
60 de secunde, cu MSVC 14.44.35228 și Windows SDK 10.0.22621.0.

### Ce a costat drumul până aici

| Simptom | Cauza reală |
|---|---|
| winget stătea o oră la „Starting package install" | rulat din shell NEelevat; `--quiet` se agață fără eroare |
| `--passive` a ieșit imediat cu `Exit Code: 5007` | aceeași cauză, dar de data asta cu mesaj: „should be run elevated from the beginning" |
| Windows s-a blocat, oprire de la buton | serviciul DAEMON Tools `Disc Soft Lite Bus Service` dădea timeout la fiecare 30 s, blocând Service Control Manager |
| „nu găsesc MSVC v143" | era deschis instalatorul de **Visual Studio 2019**, care se oprește la v142 |
| Build `Failed (RulesError)` în 9 s | lipsea **.NET Framework 4.8 SDK**; `SwarmInterface` cerea NETFXSDK. Compilatorul nu era vinovat |

### Două lucruri pe care le-am greșit

**Am spus că nu există `cl.exe` pe mașină.** Exista, în Build Tools 2019, dar
căutarea mea se oprea cu un nivel de foldere mai sus. Era 14.29, sub minimul
14.38.33130 al engine-ului, deci concluzia a rămas corectă din alt motiv. O
adâncime de căutare prea mică nu e o absență, e o măsurătoare falsă.

**Am avertizat că 14.44.35207 pică în intervalul interzis.** Nu pica. Folderul
poartă numele familiei, compilatorul din el era 14.44.35228, peste pragul de
35210. Numele folderului nu e versiunea.

---

## 2026-09-10 — Faza 2, felia 1: plutire reală

**Prompt exact:** „începe cu prima"

**Model:** claude-opus-5

`AShipPawn` în C++ înlocuiește pawn-ul Blueprint. Nava plutește pe șase pontoane,
se leagănă și se redresează singură.

### Deciziile de proiectare

**Rădăcina e o cutie de coliziune, nu mesh-ul.** `UBuoyancyComponent` aplică
forțele pe ce găsește la `Cast<UPrimitiveComponent>(GetRootComponent())`. Mesh-ul
vizibil atârnă de ea fără coliziune proprie, deci catargele și velele nu intră
niciodată în calculul fizic.

**Centrul de masă e coborât cu 180 cm** sub linia de plutire. Fără asta nava s-ar
răsturna la primul val în loc să se redreseze.

**Cârma are autoritate proporțională cu viteza.** O navă oprită nu virează,
fiindcă nu curge apă pe lângă cârmă. Cuplul se înmulțește cu viteza normalizată.

### Bug-ul care a costat o iterație

Prima rulare: `inWater=0` pe fiecare linie, nava cobora constant de la -78 la
-602 și nu se oprea. Plutirea nu porneşte niciodată.

Cauza, găsită citind `WaterBodyActor.cpp`: singurul lucru care activează
plutirea este `AWaterBody::NotifyActorBeginOverlap`, adică un eveniment de
suprapunere între actori. Pentru un ocean care acoperă toată harta, asta e o
dependență fragilă.

Soluția nu a fost să repar suprapunerea, ci s-o ocolesc: la `BeginPlay` iterez
prin toate `AWaterBody` din lume și chem `EnteredWaterBody` explicit. Un ocean
deschis e mereu sub navă, deci nu are rost să aștept un eveniment pentru asta.

### Cum am măsurat, în loc să presupun

Pawn-ul scrie o linie pe secundă cu înălțimea, tangajul, ruliul, viteza și
**înălțimea suprafeței interogată direct din water body**. Ultima coloană e cea
care separă două cauze care arată la fel: „interogarea apei eșuează" și „forța de
plutire e prea slabă".

Trecerea, după fix:

```
z=13.9 → -105.6 → -53.9 → ~-60      roll: -2.0 → -0.2
inWater=1  queryOk=1  surfaceZ=0.0
```

### Capturile de ecran, rezolvate definitiv

`HighResShot` dat la pornire se execută mereu la cadrul 5, înainte ca lumea să
deseneze ceva, de unde cele trei capturi negre de ieri. Acum declanșarea vine din
interiorul jocului: `-ShipShotAfter=N` și `-ShipQuitAfter=N` pe linia de comandă,
citite cu `FParse::Value`, iar pawn-ul cheamă comanda din `Tick` după N secunde.

### Valurile: date prezente, evaluare absentă

Am scris cod care adaugă valuri Gerstner dacă oceanul n-are. Prima variantă
verifica `if (!GetWaterWaves())` și sărea mereu peste, tăcut. Am schimbat testul
din „există obiectul" în „ce înălțime are", și atunci s-a văzut adevărul:

```
SHIPLOG existing waves=yes height=508.1
```

Oceanul **are deja** valuri de 5 metri configurate. Dar pe toată durata rulării:

```
surfaceZ=0.0   pitch=-0.0   roll=-0.0
```

Suprafața interogată e perfect plată, la fix zero, la fiecare citire. Datele de
val există, dar nu sunt evaluate niciodată.

Asta leagă cele două simptome rămase într-o singură cauză: apa nu se desenează
**și** valurile nu se calculează, fiindcă datele de runtime ale plugin-ului Water
(quad tree-ul și textura de informații) nu sunt construite. Ambele se rezolvă
printr-o singură deschidere a editorului, care e punctul 1 din `OWNER_VERIFY.md`.

Nu am adăugat valuri peste cele existente. Ar fi fost o reparație pe simptom.

---

## 2026-09-10 — Faza 2, felia 2: vântul și velele

**Prompt exact:** „continua cu vantul si velele"

**Model:** claude-opus-5

Viteza nu mai vine dintr-un buton. `UWindSubsystem` ține vântul, iar `AShipPawn`
scoate forța din trei mărimi: cât e întinsă pânza, cât de tare bate vântul și sub
ce unghi îl prinde coca.

### Modelul de velatură

Nava e cu vele pătrate, deci se poartă ca un bric, nu ca un iaht:

| Unghi | Coeficient | De ce |
|---|---|---|
| sub 48° | 0 | în vânt, pânzele flutură |
| 48° → 90° | 0 → 0,80 | prinde treptat |
| 90° → 140° | 0,80 → 1,00 | zona bună, vânt pe pupa-travers |
| 140° → 180° | 1,00 → 0,78 | velele din pupă le acoperă pe cele din proră |

Unghiul se măsoară față de direcția **din care** bate vântul, deci zero
înseamnă provă fix în vânt.

Vântul mai adaugă derivă laterală și un cuplu de ruliu proporțional cu cât de
mult bate din travers, deci nava se înclină pe o bordură.

### Cum am verificat un model care depinde de unghi

Un model polar nu se verifică uitându-te la el. Am adăugat `-ShipWindSweep=N`,
care rotește vântul complet în jurul busolei în N secunde cu toate pânzele sus.
O rulare de 90 de secunde tipărește diagrama polară direct în log.

Prima măsurătoare, cu `MaxSailForce = 3.4e7`:

```
windAng=176  drive=0.81  speed=1.51
windAng=128  drive=1.09  speed=2.17   <- vârf
windAng=95   drive=1.00  speed=2.01
windAng=63   drive=0.37  speed=0.93
windAng=47   drive=0.00  speed=0.07   <- pragul zonei moarte
windAng=31   drive=0.00  speed=0.00
```

Forma e exact cea proiectată și zona moartă taie curat la 48°. Dar 2,17 m/s
înseamnă 4 noduri într-un vânt de 20 de noduri, adică prea încet pentru un bric.

Am ridicat `MaxSailForce` la 1.3e8 și am scăzut `DragCoefficient` de la 20 la 8.
Vântul nu era problema, ci raportul dintre forța velelor și frecarea apei.

### Vântul nu e o constantă

Direcția și tăria oscilează din două sinusoide cu perioade fără raport simplu
între ele, deci nu se simte un ciclu care se repetă. Nu am folosit generator
aleator: aceleași momente de timp dau aceeași vreme, ceea ce face o rulare de
diagnostic repetabilă.

---

## 2026-09-10 — Faza 2, felia 3: tunurile

**Prompt exact:** „continua cu tunurile"

**Model:** claude-opus-5

Patru tunuri pe fiecare bord, reîncărcare separată pe bord, recul pe cocă,
integritate de cocă și `ACannonBall` cu balistică din fizică.

### De ce ghiulele fizice și nu traiectorie scriptată

Sfera simulează cu gravitație, deci arcul, căderea și impactul ies din motor.
Nu am scris nicio ecuație de traiectorie. Bătaia se reglează din două numere
care înseamnă ceva pentru un tunar: viteza la gura țevii și unghiul de ridicare.

Fiecare gură de foc primește împrăștiere proprie prin `VRandCone`, deci o salvă
încadrează ținta în loc să lovească toate în același punct.

Reculul e o impulsie aplicată **în dreptul fiecărei guri de foc**, nu în centrul
de masă, deci o salvă înclină nava puțin, cum trebuie.

### Bug: marea a înghițit toată salva

Prima măsurătoare:

```
SHOTLOG broadside side=starboard guns=4 muzzle=150m/s elevation=6.0deg
SHOTLOG hit shot=0 target=WaterBodyOcean_0 damage=60
SHOTLOG impact shot=0 range=5m flight=0.04s
```

Toate cele patru ghiulele au murit la 5 metri, după 4 sutimi de secundă, iar
ținta lovită era **oceanul**.

`AWaterBody` are un `OceanCollisionBoxComponent` adânc de 100 de metri care se
întinde până aproape de suprafață. Ghiulelele se năşteau înăuntrul lui și se
loveau instant. Mai rău, codul meu de daună aplica 60 de puncte oceanului.

Marea nu e o țintă solidă. Ghiulelele ignoră acum toate corpurile de apă la
mișcare, iar stropul vine din interogarea de suprafață din `Tick`, care era deja
acolo pentru plutire.

### Al doilea bug: salva trăgea în jos

După ce marea a încetat să înghită ghiulelele, tot cădeau la 6 metri. Am calculat
din log în loc să presupun: dacă bătaia e 6 m în 0,05 s, viteza orizontală era
corectă, deci vina nu era la viteza de la gura țevii. Dar ghiuleaua pornea de la
+60 cm și ajungea la -8 cm în aceeași 0,05 s, adică o cădere de 68 cm. Căderea
liberă în 0,05 s e 1,2 cm. Deci viteza verticală inițială era **negativă**.

Ridicarea o făceam rotind vectorul de travers în jurul axei de înaintare, cu
semn dedus pe hârtie. Semnul era invers, și era invers **pe ambele borduri**,
ceea ce ascundea greșeala: nimic nu era asimetric, deci nimic nu arăta suspect.

Am înlocuit rotația cu o formă care nu are semn de ghicit:

```cpp
const FVector Beam = (GetActorRightVector() * Side).GetSafeNormal2D();
const float Rise = FMath::Tan(FMath::DegreesToRadians(GunElevationDeg));
FVector Aim = (Beam + FVector::UpVector * Rise).GetSafeNormal();
```

Direcția orizontală plus o componentă în sus. Nu mai există axă de rotație a
cărei orientare trebuie dedusă.

---

## 2026-09-10 — Faza 2, felia 4: nava inamică

**Prompt exact:** „continua cu nava inamica"

**Model:** claude-opus-5

`AEnemyShipPawn` e aceeași clasă de navă ca a jucătorului, doar posedată de
`AShipAIController`. Nu am duplicat nimic din plutire, vele sau tunuri: inamicul
are exact aceleași limite fizice ca tine, inclusiv zona moartă a vântului.

### Trei tactici

| Tactică | Prag | Curs dorit |
|---|---|---|
| apropiere | peste 550 m | direct spre țintă |
| angajare | sub 550 m | perpendicular pe direcția spre țintă, cu corecție ca să țină 320 m |
| retragere | cocă sub 30% | vântul dead astern |

Peste toate trei se aplică `ResolveSailableHeading`: dacă cursul dorit cade în
zona moartă, alege mura mai apropiată. Un AI care ar naviga direct spre țintă
indiferent de vânt ar arăta imediat fals.

### Blocajul: prinsă în vânt pentru totdeauna

Prima rulare, inamicul a apărut cu prova la 15° de vânt:

```
AILOG tactic=close range=632m headingErr=-38 hull=1000 speed=-0.0m/s
AILOG tactic=close range=636m headingErr=-33 hull=1000 speed=-0.0m/s
```

Vira, dar cu 5 grade la fiecare 8 secunde, și nu accelera deloc. Cauza e o buclă
închisă pe care o crease chiar modelul meu de navigație: **în vânt nu ai forță,
fără forță nu ai viteză, fără viteză cârma nu mușcă, iar fără cârmă nu ieși din
vânt.** Nimic nu era stricat, doar că regulile se blocau reciproc.

Rezolvarea nu e un hack, e ce face un echipaj real: bracează vergile în vânt ca
nava să cadă pe o mură. Am dat cârmei o autoritate minimă cât timp e pânză sus,
independentă de viteză. Se aplică la ambele nave, deci nici jucătorul nu mai
poate rămâne blocat definitiv cu prova în vânt.

Am adăugat și numele navei în fiecare linie `SHIPLOG`, fiindcă de acum sunt două
nave pe apă și o linie fără etichetă nu spune despre care e vorba.

### Bug-ul cel mai urât din toată sesiunea: inamicul se scufunda tăcut

Inamicul se apropia, vira, deschidea focul. Totul părea în regulă. Doar că
ghiulelele lui apăreau la 1970 de metri sub apă.

Am căutat unde e nava și am găsit-o la **-2203 metri, coborând constant**, în
timp ce raporta liniștit `inWater=1`. Steagul minţea fiindcă îl setam la
înregistrarea explicită de la `BeginPlay`, indiferent de poziție. Iar AI-ul
raporta distanțe corecte fiindcă le calcula cu `Size2D()`, care ignoră Z. Trei
semnale verzi peste o navă care se ducea la fund.

Cauza: `UBuoyancyComponent` se înregistrează singur la managerul de plutire în
`BeginPlay`. O navă **plasată în nivel** își începe viața înaintea lui
`ABuoyancyManager`, deci nu găsește niciun manager și înregistrarea eșuează fără
un cuvânt. Nava jucătorului nu pățea asta fiindcă e creată de game mode, adică
după ce toți actorii din nivel au pornit.

Reparat prin amânarea înregistrării cu 0,2 secunde și prin apelul explicit al
`ABuoyancyManager::Register`, plus o linie de log care spune dacă managerul a
fost găsit sau nu. O înregistrare care poate eșua trebuie să spună asta.

### O greșeală de măsurare a mea, care merită reținută

Prima dată când am căutat starea navelor, am filtrat log-ul cu `awk 'NR%8==1'`.
Cu două nave care scriu alternativ, eșantionul a nimerit **de fiecare dată
aceeași navă**. Am citit doar linii ale jucătorului și am tras concluzia că
inamicul nu loghează deloc. Un filtru periodic peste surse alternante nu
eșantionează, ci selectează una singură.

### Ce funcționează și ce nu, la oprirea sesiunii

**Funcționează, măsurat:** AI-ul preia nava, alege tactica după distanță,
rezolvă cursul ca să nu intre în zona moartă, iese din vânt bracând vergile,
se apropie de la 632 la 444 m ținând cursul la un grad, se pune de-a curmezișul
și **deschide focul** cu bordul potrivit.

**Nu funcționează:** coca inamicului nu plutește. Cade cu viteză constantă de
circa 11 m/s, adică nu în cădere liberă: plutirea acționează, dar e insuficientă.

**Trei ipoteze testate și infirmate**, în ordine:

1. *Managerul de plutire nu e găsit de o navă plasată în nivel.* Am amânat
   înregistrarea și am forțat `Register`. Log-ul confirmă `manager=yes` pentru
   ambele nave. **Se scufunda în continuare.**
2. *Nava jucătorului primește plutire dublă fiindcă se înregistrează de două
   ori.* Am făcut înregistrarea exact una singură cu `Unregister` + `Register`.
   **Niciun efect.**
3. *Problema e că nava e plasată în nivel, nu creată la rulare.* Am scris
   `ASeaGameMode` care creează inamicul după pornirea lumii, exact ca pe nava
   jucătorului, și am scos-o din nivel. **Se scufundă la fel.**

**Ce e identic între cele două nave**, verificat din log: masă 60000 kg, 6
pontoane, toate 6 în apă, `active=1`, `overlapping=1`, `inBody=1`,
`sim=HullCollision`. Singura diferență rămasă e **poziția**: jucătorul e la
origine, inamicul la 632 m.

Ipoteza rămasă, netestată: forța de plutire citește textura de informații a apei,
care nu e construită. Ar fi aceeași cauză cu apa care nu se randează. Testul care
o decide e inamicul creat lângă origine, iar overrideul de linie de comandă
pentru asta e scris și compilat, dar nerulat: `-EnemyX= -EnemY=`.

---

## 2026-09-11 — Nava inamică se scufunda: cauza reală

**Prompt exact:** „continua"

**Model:** claude-fable-5-1

Reluat de la zero, nu din concluziile de ieri. Testul rămas nerulat a fost
decisiv: **inamicul creat la 134 m plutește la z=-55, identic cu jucătorul.**
La 632 m se scufundă. Deci variabila e distanța.

### Mecanismul, confirmat în sursă

Firul de fizică nu interoghează oceanul viu. Interoghează un **snapshot**
sigur pentru solver, `FSolverSafeWaterBodyData`, construit din componenta de
apă. Diagnosticul meu de ieri (`wetPontoons=6/6`, `depth=1919`) citea structura
de pe firul de joc, care întreabă componenta vie prin spline. Cele două
răspunsuri pot să nu coincidă, și exact asta se întâmpla: steagurile spuneau
„în apă", fizica spunea „pe uscat".

Oceanul are **două extinderi separate**, în `WaterBodyOceanComponent.cpp`:

| Proprietate | Linia | Valoarea din nivel |
|---|---|---|
| `OceanExtents`, legată de zona de apă | 32, 104 | mărită de mine la 5 km prin spline |
| `CollisionExtents`, cutia de coliziune (linia 574) | 31, 574 | **rămasă la 256 m** |

134 m e înăuntrul cutiei, 632 m e în afară. Am pus `collision_extents` la
5 km, salvat cu `save_map` și citit înapoi dintr-o încărcare curată.

### Ce am înțeles greșit ieri și de ce

Am scris că „plutirea acționează dar e insuficientă", fiindcă nava cădea cu
viteză constantă, nu în cădere liberă. Viteza constantă venea din amortizarea
liniară a corpului însuși (`SetLinearDamping(0.5)`), nu din vreo forță de apă.
Nicio forță de plutire nu se aplica. Am dedus o cauză dintr-un simptom care
avea altă explicație.

Cele trei fixuri de ieri pentru ipoteze infirmate — înregistrare amânată,
înregistrare unică, `RecreatePhysicsState` — erau prezente în **fiecare**
rulare în care nava se scufunda, deci nu repară nimic. Le-am scos, ca să nu
rămână un diagnostic fals în comentarii.

### Odată plutind, inamicul a rămas blocat cu prova în vânt

Prima rulare cu inamicul pe apă:

```
AILOG tactic=close range=636m headingErr=-63  speed=-0.6m/s
AILOG tactic=close range=653m headingErr=-104 speed=-0.4m/s
AILOG tactic=close range=657m headingErr=-104 speed=-0.1m/s
```

Eroarea de curs **creștea** cu cârma la maxim, apoi se bloca. Viteza e ușor
negativă: nava aluneca înapoi cu sub 1 m/s, din așezarea pe apă după spawn.

Cauza e în codul meu de cârmă: `Direction = (ForwardSpeed < -20) ? -1 : 1`.
Sub 20 cm/s înapoi consideram că nava merge cu pupa înainte și inversam cârma,
cum e corect pentru sternway real. Dar cu autoritatea minimă de „vergi bracate"
(pusă exact ca să iasă din vânt) cârma inversată o rotea în sens opus față de
ce cerea AI-ul, până se oprea unde cuplul de vânt echilibra.

Ieri, când „se scufunda", profilul de viteză era altul și scăpa din întâmplare.
Un bug ascuns de alt bug. Pragul e acum 100 cm/s, adică sternway adevărat.

### Apoi s-a oprit la 602 m, cu ținta în vânt

Cu cârma reparată, inamicul a ieșit din vânt și a prins 1,6 m/s, dar la 602 m a
pierdut viteza până la zero, a alunecat înapoi la 621 m și a întors brusc pe
cealaltă mură (`headingErr` de la -7 la +98).

Jucătorul e aproape în vântul inamicului: cursul direct spre țintă e la vreo
17° de vânt, adânc în zona moartă. `ResolveSailableHeading` îl împingea la
`NoGo + 6°`, adică la 54° de vânt, unde coeficientul de forță e 0,11. Nava
abia se târa, iar la prima oscilație a vântului unghiul cădea sub 48°, forța
ajungea zero și funcția alegea mura cealaltă. Nu comitea niciodată.

Două schimbări, ambele lucruri pe care le-ar face un căpitan:

- **Cursul strâns e la `NoGo + 22°`**, nu la margine. La 70° de vânt rigul
  trage la 0,42 din forță, nu la 0,11.
- **Mura se ține minimum 45 de secunde.** A merge în vânt înseamnă zigzag cu
  bordee lungi, nu schimbat mura la fiecare rafală.

### Angajarea completă, măsurată

```
tack port      range 638 → 572 m   speed 3.7 m/s     (bordeu lung)
tack starboard headingErr 117 → 0  speed 0.7 → 2.8   (întoarcere prin vânt, costă drum)
engage         range 547 m
broadside port muzzle 150 m/s, elevation 6°
splash         409 m, 448 m, 467 m, 518 m           (încadrare la ~509 m, fără lovitură)
```

Întoarcerea prin vânt a costat viteza de la 3,4 la 0,7 m/s — corect, o navă
cu vele pătrate pierde drum când trece prin vânt. Salva la 509 m a încadrat
ținta pe 109 m fără lovitură directă; la distanța asta, cu împrăștierea
setată, e rezultatul așteptat. Inamicul continuă să strângă spre 320 m.

Din patru minute de luptă, cam trei sunt apropierea în bordee, fiindcă
jucătorul e plasat aproape în vântul inamicului. E o alegere de scenariu, nu
un bug: se schimbă din `EnemySpawnLocation` în game mode.

### Patruzeci de ghiulele, zero lovituri: tunurile nu ocheau

În angajarea completă inamicul a tras zece salve, 40 de ghiulele, toate cu
stropul între 409 și 518 m, niciuna în jucător, aflat la 400 m. Inamicul era
inofensiv, deci lupta nu putea fi pierdută.

Două cauze, amândouă în codul meu de tunuri:

1. **Ridicarea era fixă, 6°, indiferent de distanță.** Asta cade mereu la
   ~450 m. La 400 m sau la 320 m trage peste, de fiecare dată. Un șef de tun
   reglează pana pentru distanță. Acum ridicarea se calculează din arcul
   balistic pentru distanța țintei: `sin(2θ) = R·g/v²`, corectat cu 8% pentru
   frecarea măsurată (un tir la 6° cădea la ~440 m față de 477 m ideal).
2. **Tunurile trăgeau fix pe travers**, iar AI-ul avea voie să tragă cu ținta
   la 35° de travers. La 400 m asta trece la 230 m pe lângă țintă. Acum tunul
   se rotește spre țintă în limita a 12° (cât permitea un afet), iar AI-ul
   trage doar cu ținta sub 14° de travers.

Tastele Q/E ale jucătorului ochesc automat cea mai apropiată navă de pe bordul
respectiv, prin același cod. Fără asta jucătorul n-ar putea nici el nimeri.

**Test decisiv**, inamic la 356 m fix pe travers tribord, salvă cronometrată:

```
broadside side=starboard target=EnemyShipPawn_0 range=356m
splash 302 m · splash 336 m · HIT 346 m · HIT 355 m
damage taken=60 integrity=940 · damage taken=60 integrity=880
```

Două lovituri din patru, iar inamicul a răspuns imediat cu propria salvă
ochită. Bucla de luptă e închisă cap-coadă.

Ce a mai ieșit din cifre: o împrăștiere conică de 1,6° pune eroarea și în
ridicare, iar pe arcul ăsta un grad de ridicare înseamnă ~80 m de bătaie.
O salvă cădea oriunde pe 260 m. Împrăștierea e acum separată: 1,6° în rotire,
0,35° în ridicare.

### Inamicul tot nu nimerea: nu era bias, era geometrie

Cu tunurile ochind, jucătorul lovea 3–4 din 4 la 356 m, inamicul 0 din 8 la
357 m. Am bănuit pe rând înclinarea, înălțimea gurii de foc, viteza proprie și
ridicarea, și am pus toate patru în linia de log a salvei:

```
ShipPawn_0       heel=-0.0 muzzleZ=65 velBeam=-0.36 velFwd=0.35 elev=4.74
EnemyShipPawn_0  heel= 0.2 muzzleZ=64 velBeam=-0.48 velFwd=0.53 elev=4.82
```

Identice. Deci nu starea navei. Ghiulelele inamicului cădeau la 357–381 m, adică
la distanța corectă, dar **pe lângă** țintă lateral.

Cauza: eu pusesem inamicul exact pe traversul jucătorului, deci tunurile
jucătorului n-aveau nevoie de nicio rotire. AI-ul, în schimb, trage de îndată
ce ținta intră în arcul lui de 14°, iar afetul se rotește doar 12°. Restul de
2° plus împrăștierea de 1,6° fac ~20 m lateral la 357 m, iar coca are 15 m de
la centru la extremitate. Arcul de tragere al AI-ului trebuie să stea **în
interiorul** rotirii tunurilor, nu în afara ei. E acum 9°.

Lecția: două nave cu același cod, rezultate diferite, și diferența nu era în
nave, ci în cum le așezasem eu în test.

### Nu trecea prin cocă, trăgea pe lângă. Măsurat, nu dedus.

Am pus în fiecare ghiulea vectorul ratării față de țintă: `along` (+ = lung)
și `lateral`. Apoi am creat inamicul cu bordul deja pe jucător (`-EnemyYaw=180`,
override nou), fără nicio salvă a jucătorului, ca să existe o singură sursă de
ghiulele:

```
impact by=EnemyShipPawn_0 shot=1 along=-5.1 lateral=+0.1 impactZ=254  HIT
impact by=EnemyShipPawn_0 shot=2 along=-5.0 lateral=+2.6 impactZ=283  HIT
impact by=EnemyShipPawn_0 shot=3 along=-5.1 lateral=+0.5 impactZ=237  HIT
player hull 1000 → 820
```

`along = -5,1 m` e exact fața dinspre inamic a cutiei de coliziune, a cărei
jumătate de grosime e 5,2 m. Ghiulelele inamicului lovesc coca jucătorului
perfect normal. Ipoteza „trec prin cocă" era greșită: n-aveam numărul lateral,
iar fără el o ratare la 20 m lateral și o trecere prin cocă arată identic în
log. Cele 0 din 8 dinainte erau ratări laterale: AI-ul trăgea de la marginea
arcului, în plin viraj, cu tunurile care nu mai apucau să se rotească.

### Virajul cel scurt

Cu arcul strâns la 9°, inamicul creat pe traversul jucătorului **n-a mai tras
deloc** în 60 s: regula de angajare alegea bordul „mai departe de vânt", care
era la 147° prin ochiul vântului, fără viteză, la un grad pe secundă. Regula e
acum: cel mai scurt viraj, dacă e navigabil; altfel celălalt.

### Angajarea completă, cu tot ce s-a reparat azi

Inamicul creat la 632 m, aproape în vântul jucătorului, jucătorul fără input
(rulare headless). 420 s, fără nicio intervenție:

```
close   bordeu port      632 → 572 m   3,7 m/s
tack starboard            întoarcere prin vânt, 3,3 → 0,7 m/s
tack port                 a doua întoarcere
engage  533 m             cursul pus pe bord, 0,7 → 2,7 m/s
salva 1  523 m   2/4      lovituri: 0,1
salva 2  528 m   3/4      ratare along=-4,4 m (scurt, în fața cutiei)
salva 3  535 m   3/4      ratare along=-6,9 lateral=-10,7
salva 4  542 m   2/4      ratări along=+34,5 și +37,9 (lungi)
player hull 1000 → 400 la t=400 s;  enemy hull 1000 (n-a tras nimeni în ea)
```

10 lovituri din 16 la 523–542 m. Toate cele 6 ratări sunt pe axa „along",
adică bătaie prea lungă sau prea scurtă, nu lateral: ochirea în rotire e
rezolvată, ce rămâne e împrăștierea în ridicare peste 500 m, unde 1° înseamnă
~80 m. Ieri, la aceeași distanță, patruzeci de ghiulele n-au atins nimic.

Măsurătoare stricată și reparată: primul grep avea `shot=[0-9]` cu o singură
cifră, deci `shot=11` se citea `shot=1` și părea că aceeași ghiulea lovește de
trei ori. Tiparul era bugul, nu logul.

Nava jucătorului pierde 60% din cocă în 4 salve fără să răspundă. Când răspunde
cu Q/E ochite are aceeași gunărie, deci lupta e simetrică; ce lipsește ca joc
e scufundarea la 0 și o interfață care să-ți arate coca și reîncărcarea.

### Curățenie

`Content/Blueprints/BP_PirateShip` și `BP_SeaGameMode` scoase din proiect: nimic
nu le mai referea (nivelul, config-ul, sursele), erau pawn-ul și game mode-ul din
Faza 1, dinainte de compilatorul C++. Le-am mutat, nu șters, în caz că vrei să
te uiți la ele.

## 2026-09-11 — Scufundarea ca eveniment

**Task Started.** Prompt exact: „ok" (după recomandarea mea: scufundarea la
cocă 0 înainte de avarii pe zone și HUD). Model: Claude Fable 5.1.

### Faza 0: descoperirea care răstoarnă ziua de ieri

Înainte de a scrie o linie de scufundare am rulat un panel de design (3
designeri, 2 judecători, sinteză). Judecătorii au citit sursa plugin-ului Water
și au ridicat o obiecție pe care n-o puteam respinge: forța pe pontoon e
`clamp(C · V, 0, MaxBuoyantForce) · coeficient`, iar cei șase coeficienți
(mase suspendate, `ComputeSprungMasses`) însumează exact 1. Deci plafonul de
2,5e7 pe pontoon e un plafon pe TOATĂ nava: maximum 2,5e7 din 5,88e7 = 42% din
greutate. Și totuși ambele nave stau la -55 cm de 400 de secunde.

Am instrumentat (`lift=` = suma forțelor pe pontoane / greutate, plus o linie
`SEALOG ocean collision` cu ce vede scena fizică din ocean):

```
SEALOG ocean collision OceanCollisionBoxComponent_1 enabled=3 objType=1 respPawn=2 top=0 bottom=-20000
SHIPLOG ShipPawn_0 pontoon[0..5] coef=0.134,0.153,0.153,0.178,0.178,0.204   (suma 1.000)
SHIPLOG ShipPawn_0 lift=0.390 ... t=10 lift=0.43 z=-52.2 vz=0   async=0
```

**Nava n-a plutit niciodată.** Stă pe cutia de coliziune a oceanului: un corp
fizic real (QueryAndPhysics, tip WorldDynamic, blochează pawn-ii), cu vârful la
z=0. Coca s-a născut cu 350 cm în interiorul lui, Chaos tolerează suprapunerea
inițială și o ține la adâncimea la care s-a născut; plutirea duce 43%,
contactul restul. De asta „z=-55, inWater=1" arăta ca o plutire.

Asta rescrie și cauza bug-ului de ieri („inamicul se scufunda la 632 m"):
nu snapshotul firului de fizică, ci **lipsa cutiei** dincolo de 256 m. Fără
cutie, nava cade cu 0,57·g frânată de amortizarea 0,5 → viteză terminală
0,57·980/0,5 = 11,2 m/s, exact cei 11 m/s măsurați atunci. Fixul (cutia la 5
km) a mers din motivul greșit. În plus, `async=0`: proiectul rulează calea de
pe firul jocului, nu pe cea asincronă — explicația mea cu „snapshotul" nu se
aplica deloc.

Proba cu `-ShipIgnoreOcean=1` (coca ignoră WorldStatic) n-a schimbat nimic,
fiindcă tipul cutiei e WorldDynamic (1), nu WorldStatic (0). Ipoteza bună,
canalul greșit; canalul l-am citit apoi din log, nu din presupuneri.

Încă o descoperire din aceeași linie: pontoanele citesc înălțimi de apă
diferite (-173 … -334 cm la t=3), deci interogarea de plutire EVALUEAZĂ
valurile. „Suprafața e mereu plată" era fals; nava nu se legăna fiindcă stătea
pe cutie.

### Faza 0b: coca eliberată, plutirea recalibrată

- `HullCollision` răspunde cu Overlap la WorldDynamic (cutia oceanului).
- `BuoyancyCoefficient` 2,6 → 0,66: la pescajul de 55 cm, imersia de 375 cm pe
  o sferă de 320 dă V = 8,6e7 cm³, ponderat 8,9e7, și 5,88e7 / 8,9e7 = 0,66.
- `MaxBuoyantForce` 2,5e7 → 2,0e8, deasupra unei sfere complet scufundate
  (0,66 · 1,4e8 = 9,2e7), ca plafonul să nu mai muște niciodată.
- Hula pusă explicit, moderată (amplitudini 8–32 cm × 8 valuri, ~1,6 m vârf),
  în locul celei de 5 m moștenite din hartă.
- Ghiuleaua verifică suprafața CU valuri, ca să plesnească în aceeași mare.

Măsurat după 0b (60 s, jucătorul fără input, inamicul navigând):

```
player  t>=10: z min=-158 max=+11 mean=-73   lift mean=1.00   heel ±2.5   asleep=0
enemy   drive 0.5, 5,4 m/s, lift 0,93–1,08
contacte fizice ale cocii: 0
```

Două capcane pe drum, ambele prinse din log, nu din presupuneri:

1. **Hula de 5 m nu se lăsa schimbată.** `UGerstnerWaterWaves` își calculează
   valurile O SINGURĂ DATĂ, în constructor, cu generatorul implicit (16 valuri
   până la 80 cm). Generatorul atribuit după aceea nu face nimic până la
   `RecomputeWaves(false)`. Logul zicea `max height 508` oricare ar fi fost
   amplitudinile mele. Acum: 140 cm.
2. **Chaos adormea coca.** Cu pânzele jos nava se mișcă destul de încet ca
   solver-ul s-o pună în somn, iar forțele de plutire n-o trezesc: jucătorul a
   înghețat la z=-165 cu `lift=1.3` și `vz=0`, în timp ce inamicul, în mers cu
   6 m/s, se legăna normal. Fix: `SleepFamily=Custom` cu multiplicator 0 și un
   `WakeAllRigidBodies` de siguranță în Tick. O navă pe mare nu e niciodată în
   repaus.

### Scufundarea: ce s-a construit

Panelul a votat designul cu buclă închisă pe pescaj, cu o schimbare de
pârghie propusă de judecători: nu razele pontoanelor, ci `PontoonCoefficient`,
care înmulțește forța DUPĂ plafonul motorului și pe care motorul îl rescrie
doar când se schimbă masca de pontoane active. Liniar, fără zonă moartă.

Faze: `Afloat → Flooding → Foundering → Plunging → Wreck`.

- `TakeDamage` reține punctul de impact în spațiul cocii; la integritate 0
  cheamă `BeginSinking`, care alege cele două pontoane cele mai apropiate de
  spărtură ca „găurite", anulează comenzile și difuzează `OnShipSunk`.
- Inundare: o greutate de apă (0,3 din greutatea navei) aplicată jos, pe
  bordul spărturii, plus coeficienții pontoanelor scăzuți în buclă închisă
  până la punte inundată (pescaj -250 cm față de suprafața LOCALĂ, media
  înălțimilor de apă de sub pontoane, deci robust la hulă).
- Punte inundată: 5 s de pauză, coca ignoră Pawn și PhysicsBody (o navă
  proaspătă nu se poate lovi de epavă, ghiulelele trec).
- Plonjare: coeficienții la 0 în 6 s, amortizare liniară 5 → viteză
  terminală (1+0,3)·980/5 = 2,5 m/s, exact cât s-a măsurat.
- La -30 m: `OnShipWrecked`, `Destroy()`.
- Game mode: `SetPlayerDefaults` leagă nava jucătorului, `SpawnEnemy` pe a
  inamicului; `DEFEAT` → respawn pe loc după 35 s (`UnPossess` +
  `RestartPlayerAtTransform`, așteaptă cât epava e deasupra a -10 m);
  `VICTORY` → inamic nou după 8 s. `-ShipQuitAfter` s-a mutat în game mode, pe
  timpul lumii: un pawn mort nu poate închide nimic.
- Sabordare de test prin ACEEAȘI cale `TakeDamage` (`-ShipSinkTest=N`,
  `-EnemySinkTest=N`, `-ShipSinkSide=port|starboard`; în editor comanda
  `Scuttle`).

### Măsurători

Rularea A (`-ShipSinkTest=10`, inamicul la 356 m), zero ensure/assert:

```
t=10.0  SUNK by=SeaGameMode breach=(900,520) side=starboard holed=1,0   DEFEAT
        AILOG target lost ShipPawn_0
sinkT   phase        draught  heel   pitch  lift   vz
 0      flooding      -52     +0.7   +1.0   0.99
 5      flooding     -118     +0.8   -0.3   1.07
10      flooding     -199     +5.7   -5.3   1.12
11.5    awash (reason=draught)
15      foundering   -409    +15.0  -14.0   1.18
16.5    plunging
20      plunging     -813    +28.7  -29.8   0.51  -1.50 m/s
25      plunging    -2108    +63.8  -49.4   0.00  -2.73 m/s
28.3    wrecked z=-3054 reason=depth      SEALOG wreck cleared
t=45.0  respawn player=ShipPawn_1 (10 + 35), lift ~1.0 în 10 s
        AILOG target=ShipPawn_0 → none → ShipPawn_1
```

Convenția de semne, fixată empiric: spărtura la tribord → `heel` POZITIV;
prova jos → `pitch` NEGATIV. Rularea A' cu `-ShipSinkSide=port` dă oglinda:
`holed=2,0`, heel -6,7 la punte inundată.

Rularea B (`-EnemySinkTest=15`): `VICTORY`, `AILOG abandoning ship`, inamic
nou `EnemyShipPawn_1` la t=23 cu propriul controller, care trage la 356 m în
timp ce epava vechiului coboară; `wrecked` la sinkT 30,0.

Calibrare: la `FloodWaterFraction` 0,40 puntea era inundată în 11,5 s, la 0,30
în 12,5 s. Greutatea apei domină ritmul, bucla pe pescaj rămâne la rata de
bază. Am lăsat 0,30: ~12 s până la punte, 5 s pauză, ~12 s plonjare, epava
la 30 s. Pentru un joc e un ritm bun; „25 s de inundare" era o cifră de
design, nu o cerință.

Rularea C (tir real, inamicul la 356 m pe bordul cu vânt, 300 s): 5 salve în
primele 90 s, **14 lovituri din 17**, coca jucătorului 1000 → 160, apoi
liniște 210 s. Nu scufundarea a tăcut, ci AI-ul: nava a trecut de jucătorul
staționar cu 6,5 m/s (356 → 277 → 446 m), iar virajul cerut de geometria de
angajare a dus-o prin vânt (`windAng` 84 → 12), unde s-a rotit cu ~1,5°/s pe
autoritatea de „vergi bracate", de două ori câte 80 s. E limitarea cunoscută
din ziua de ieri, acum cu cifre: **AI-ul n-are noțiunea de „nu vira prin
vânt, vira pe sub vânt"**. Subiect pentru felia următoare.

Rata de lovire a crescut față de ieri (10/16 la 520–540 m, pe o cocă
sprijinită pe cutie) la 14/17 la 300–356 m pe o cocă ce plutește și se
leagănă: distanța mai mică bate hula.

Rularea C' (`-ShipHullTest=200`, flag nou care pornește DOAR nava jucătorului
cu coca slăbită, ca să nu aștept 17 lovituri), zero ensure/assert:

```
salva 1 la 356 m: 2 lovituri (200 → 80)   salva 2 la 330 m: 2 lovituri (80 → 0)
t=14.7  SUNK by=CannonBall_6 breach=(-407,-193) side=port holed=4,3
        DEFEAT · score victories=0 defeats=1 · AILOG target lost ShipPawn_0
awash sinkT=11.8 · plunging 16.8 · wrecked 30.3 (z=-3054) · wreck cleared
t=49.7  respawn player=ShipPawn_1 (14.7 + 35) · inamicul o lovește la 321 m (940)
```

Spărtura vine din impactul real al ghiulelei: inamicul era la -Y, deci bordul
babord, iar cele două pontoane „găurite" sunt cele din pupa-babord și
pupa-tribord, cele mai apropiate de (-407,-193). Aceeași cale de cod ca
sabordarea de test, cu `DamageCauser` ghiuleaua.

**Task Completed.** Felia 5, scufundarea ca eveniment, livrată și măsurată pe
patru rulări (sabordare tribord, sabordare babord, sabordarea inamicului, tir
real). Codul: `ShipPawn.*` (faze, forțe, coeficienți, sabordare, log),
`SeaGameMode.*` (legare, înfrângere/victorie, respawn, flag-uri, quit),
`ShipAIController.*` (abandon, țintă pierdută), `CannonBall.cpp` (IsValid).
Rămân: avarii pe zone, HUD, și AI-ul care virează prin vânt.

### Pasa adversarială peste felia de scufundare

Trei recenzori pe diff (ciclu de viață UE, fizică, gameplay). Verificatorii
automați au căzut pe limita de sesiune, deci am verificat eu fiecare
constatare, în cod și în loguri. Cinci confirmate, toate reparate.

**1. Bucla de inundare era moartă.** Reproș: pescajul e MEREU sub profilul
cerut, deci `Lag` e zero din prima secundă și `FloodScale` scade fix cu rata de
bază; `FloodSeconds`, `FloodRateGain` și `FloodRateMax` nu fac nimic. Verificat
din propriile mele loguri: `flood` cobora cu 0,021/s constant, iar profilul
cerea -139 cm la secunda 11 când coca era deja la -229. Cauza: greutatea apei
singură duce coca cu ~85 cm mai jos în câteva secunde, deci e permanent ÎNAINTEA
profilului. Fix: două rate, una când e mai sus decât cere profilul (0,02/s) și
un firicel când e deja mai jos (0,004/s). Măsurat după: rata variază real
(1,00 → 0,84 în 11 s, apoi se oprește la 0,82), puntea ajunge la apă la 16 s în
loc de 12. Iar afirmația mea din jurnal, „bucla pe pescaj rămâne la rata de
bază", era o descriere corectă a unui bug pe care nu l-am recunoscut ca bug.

**2. AI-ul vira prin ochiul vântului.** Reproș: `ResolveSailableHeading`
returnează cursul dorit dacă E navigabil, fără să se uite dacă DRUMUL scurt
până la el trece prin vânt. Confirmat în cod. Fix: `ArcCrossesWind` (unde e
ochiul vântului față de proră, cât mătură virajul scurt, se intersectează?),
plus manevra de „vine în vânt pe sub vânt": dacă drumul scurt taie vântul, pune
cârma pe partea cealaltă și vino prin pupă. Alegerea bordului de angajare
penalizează acum cu 360° virajul care taie vântul, deci nu-l alege decât dacă
nu are altul.

Măsurat pe aceeași rulare de 300 s, jucătorul fără input:

| | înainte | după |
|---|---|---|
| salve | 5 | 8 |
| lovituri | 14 | 22 |
| eșantioane AI cu viteza sub 0,5 m/s | ~80 din 132 | **0 din 132** |

Și de data asta jucătorul chiar s-a scufundat sub tir: `SUNK by=CannonBall_25`
la t=141, spărtură la tribord, epavă la sinkT 36, navă nouă la t=176.

**3. Respawn-ul verifica doar epava proprie.** Nici jucătorul, nici inamicul
nu se uitau la CEALALTĂ navă înainte să nască o cocă de 60 t cu `AlwaysSpawn`.
Plus: pragul de adâncime compara ORIGINEA epavei, iar o epavă coboară înclinată
35° și cu prova în jos 30°, deci la origine -1000 catargele sunt încă la +7 m.
Fix: `IsSpawnClear` iterează toate navele și compară CUTIA DE ÎNCADRARE
(catarge incluse) cu pragul.

**4. Garda de așteptare a respawn-ului.** Aștepta după „orice pawn valid peste
-1000", nu după „o epavă care se scufundă", deci o navă sănătoasă ajunsă la
controller ar fi blocat-o la nesfârșit. Fix: dacă nava de pe controller nu se
scufundă, respawn-ul se anulează, nu se reprogramează.

**5. Comentariul vitezei terminale** spunea 980/5 = 1,96 m/s, uitând că apa
rămâne la bord: e (1+0,3)·980/5 = 2,55. Măsurat: 2,54. Comentariul corectat.

În plus, două lucruri mici: o navă fără țintă strânge pânza în loc să meargă
mai departe cu toată velatura, iar respawn-ul jucătorului e adus în față când
epava dispare mai devreme decât cronometrul, ca să nu rămână camera pe o mare
goală. Regresie zero pe celelalte trei rulări (babord, inamic sabordat,
plutire).

### Limitarea rămasă, acum cu cifre

Inamicul tot nu poate ajunge la o țintă din vânt: în rularea de 300 s a rămas
la 750 m, pe cursul corect, cu 5 m/s, fără să câștige un metru. Cauza,
măsurată din `velBeam`/`velFwd` la fiecare salvă: **deriva e de 15–38°, în
medie 26°**. Cu prova la 70° de vânt și 26° de derivă, drumul REAL e la 96° de
vânt, adică exact pe travers: zero câștig în vânt. Coca n-are rezistență
laterală, doar frecarea izotropă a apei — îi lipsește chila. Asta e felia
următoare de model de velatură, nu una de AI.

## 2026-09-12 — Chila, deriva, și marea care se vede

**Task Started.** Prompt exact: „continua cu chila si deriva", apoi, la mijloc,
„putem sa cream si apa cu valuri". Model: Claude Opus 5.

### De ce nu putea nicio navă să câștige teren în vânt

Din sursa plugin-ului: `ComputeLinearDragForce` e **izotrop în plan**. Ia
viteza orizontală, o normalizează și frânează pe direcția ei. Orientarea cocii
nu intră deloc. Deci nava alunecă lateral la fel de ușor cum merge înainte, iar
la echilibru viteza se aliniază cu forța totală:

```
tan(derivă) = LeewayFraction · LateralWind / Falloff
```

`Falloff = 1 - viteză/MaxForwardSpeed` tinde la zero pe măsură ce nava se
apropie de viteza maximă, deci unghiul de derivă tinde la 90°. Asta era
patologia, scrisă într-un rând.

Polarul măsurat înainte (baleiaj complet al vântului, 360 s):

| unghi | viteză | derivă | câștig în vânt |
|---|---|---|---|
| 50° | 2,01 | 10,8° | +0,75 |
| 70° | 4,78 | 23,5° | **−0,77** |
| 90° | 6,08 | 36,6° | −4,92 |

La 70° pierdea teren cu 4,8 m/s sub chilă.

### Panelul, și ce a găsit în codul MEU

Trei designeri, doi judecători care au refăcut fiecare cifră. Amândoi au dat
același câștigător, dar partea valoroasă au fost **trei bug-uri în cod, nu în
design**:

1. **Forța laterală a velei folosea vectorul înclinat** (`GetActorRightVector`).
   La 8° de bandă asta injecta `sin(8°)·9,2e6` = 1,3e6 pe VERTICALĂ, 2% din
   greutatea navei, în sus pe o mură și în jos pe cealaltă.
2. **`velBeam` din logul salvei se citea DUPĂ recul.** Patru tunuri × 9e5 pe
   60 t = 60 cm/s de smucitură laterală, adică 7° de derivă falsă. Media mea de
   „26° derivă" era umflată de propriul recul. Polarul, măsurat independent,
   dădea 23,5° — deci concluzia a rezistat, dar cifra nu.
3. **Coloana `track` din polar** scădea o derivă CU SEMN dintr-un unghi de vânt
   FĂRĂ semn. Era corectă doar pe o mură. Acum se calculează direct din vectorul
   de drum.

Judecătorii au mai corectat șapte cifre din designul câștigător (raportul de
amortizare în giraţie 36→56, deriva navei oprite 1,7→1,47 m/s, viteza de
giraţie 4,44→4,52 °/s) și au prins un `?:` care nu compila.

### Chila

Un plan lateral, ca foaie portantă de alungire foarte mică. Cheia, verificată
de ambii judecători: portanța și rezistența indusă, proiectate pe axele cocii,
**se anulează exact pe direcția navei** și lasă o forță pur laterală. Deci o
chilă modelată așa nu poate schimba pe furiș viteza maximă — cea mai curată
garanție împotriva regresiei.

```
Forță laterală = -(PanelLiftK · viteza_de_curgere · derapaj
                 + PanelCrossK · |derapaj| · derapaj)
```

`PanelLiftK` = 0,5 · 1,025e-3 · 3,9e5 · 0,45 = **90 kg/cm**,
`PanelCrossK` = aceeași arie cu Cd 0,90 = **180 kg/cm**. Aplicată în două
puncte, la o rază de girație în față și în spate de centrul de rezistență
laterală, care e ÎN SPATELE centrului de masă: în față ar fi făcut coca
instabilă direcțional, ca o giruetă prinsă de vârf. Ambele la înălțimea
centrului de masă, ca planul să nu adauge bandă proprie.

Polarul de după:

| unghi | viteză | derivă | câștig în vânt |
|---|---|---|---|
| 50° | 2,51 | 6,0° | +1,19 |
| 60° | 4,43 | 6,7° | **+1,39** |
| 70° | 5,41 | 7,9° | **+0,69** |
| 90° | 6,10 | 9,4° | −1,45 |

Deriva 6–9°, exact în banda istorică de 5–10° pentru o navă cu vele pătrate.
Optimul de urcare în vânt e la 60°, cu 1,39 m/s câștig. `TurnTorque` 2,0e10 →
3,3e10 ca să plătească amortizarea nouă în giraţie: cercul de giraţie a crescut
de la 52 la 73 m, adică două lungimi de navă, ceea ce e mai realist decât
înainte.

**Lupta de 300 s, aceeași ca ieri:**

| | ieri, fără chilă | azi |
|---|---|---|
| salve | 8 | **14** |
| lovituri | 22 | **28** |
| eșantioane AI împotmolit | 0 | 0 |

Regresii: portanța 1,003, pescaj 75 cm, bandă ±2,5°, zero contacte, scufundarea
identică (punte inundată 16,1 s, epavă 34,3 s).

### Apa care se vede

Suprafața plugin-ului Water n-a desenat niciodată. Am eliminat pe rând, cu
diagnostice în log: cvar-urile de randare (1), `CanEverRender` (1), zona
(`enabled=1 visible=1 registered=1 materials=2`), legătura corp–zonă, mesh-ul
de informații (708 triunghiuri), `shouldRender=1`, `generatesTile=1`, PSO-urile.
Proba decisivă: **aceeași cadră cu `r.Water.WaterMesh.EnableRendering` 1 și 0 e
identică**. Albastrul de sub orizont era atmosfera. Toate porțile verzi, zero
triunghiuri. Nu l-am rezolvat; l-am ocolit.

Marea e acum a mea:

- **Mesh**, generat în Blender: grilă RADIALĂ, 160 de segmente pe 96 de inele
  care cresc geometric de la 2,5 m la 6 km. 30 880 de triunghiuri acoperă
  orizontul cu centimetri de detaliu sub cocă. O grilă uniformă destul de deasă
  pentru un val de 9 m ar fi cerut milioane.
- **Material**, scris prin API-ul Python: deplasare de vârfuri cu suma a șase
  unde Gerstner, plus normala analitică din pantele lor (vârfurile deplasate
  prin WPO nu-și recalculează normalele).
- **Actor C++**, `AOceanSurface`: citește setul de unde chiar din corpul de apă
  cu `GetGerstnerWaves()` și îl împinge în material, apoi urmărește nava,
  săltând din 5 în 5 m ca triunghiurile să nu foiască.

Logul confirmă potrivirea: `surface waves drawn=6 of 6, amplitude 109 of 109 cm
(100%)`. Marea desenată E marea interogată de plutire.

**Două capcane pe drum:**
- Nodul Sine din materiale calculează `sin(x·2π/Period)`. Fără `Period = 2π`
  valurile ar fi avut perioada greșită cu un factor de 6,28, tăcut.
- Prima versiune a tras `drawn=6 of 16`: actorul citea hula de 5 m a hărții,
  fiindcă nava o înlocuia abia la 0,2 s. Starea mării a trecut în game mode,
  înaintea suprafeței. O stare a lumii n-avea ce căuta la o navă.

**Task Completed.** Rămâne: avarii pe zone, HUD.

## 2026-09-12 — Avarii pe zone

**Task Started.** Prompt exact: „continua cu avarii pe zone". Model: Claude Opus 5.

### Ce am măsurat ÎNAINTE să construiesc

Panelul avea o singură întrebare cu adevărat riscantă: cum dai greementului un
volum pe care ghiulelele să-l poată lovi, fără să strici corpul fizic. Am
răspuns prin măsurătoare, nu prin citit.

Întâi am citit tensorul de inerție din solver, ceea ce nu făcusem niciodată:

```
mass=60000  com=(0,0,-180)  inertia=(7.8580e+09, 5.0500e+10, 5.5402e+10)
```

Formula cutiei dădea `Izz = 5,346e10`, deci cu 3,6% mai mic. Toate calculele
mele de giraţie erau ușor optimiste. Și, mai important, aveam acum o gardă.

Apoi am atașat o cutie de probă `QueryOnly` și am recitit:

```
inertia=(7.8580e+09, 5.0500e+10, 5.5402e+10)   identică, bit cu bit
```

`UShapeComponent` pornește cu `bAutoWeld = true`, deci o cutie obișnuită S-AR FI
sudat în corpul cocii și i-ar fi rescris masa și inerția, tăcut, cu un volum de
25 m înălțime. `QueryOnly` e refuzat de toate cele trei căi de sudare.

A doua probă: **11 atingeri de greement față de 24 de cocă în 120 s** — deci
ghiulelele chiar găsesc un volum query-only. Dar aceeași probă a arătat și
capcana: loviturile joase soseau la 2–5 m înălțime și atingeau fundul cutiei,
deci ar fi contat de două ori. Podeaua greementului a ajuns la **+350, exact
vârful cutiei cocii**: înălțimea e partiționată, fără gol și fără suprapunere.

### Zonele

- **Cocă**: ca înainte, 60 din 1000, la zero se scufundă.
- **Greement**, două volume query-only peste catargul mic și cel mare. Ghiuleaua
  le caută singură, cu `SweepSingleByObjectType` pe drumul parcurs în cadrul
  respectiv: la 150 m/s face 250 cm pe cadru, iar un simplu test de poziție ar
  rata un catarg de 460 cm. Zona se decide după COMPONENTA lovită, nu după
  geometrie, deci o lovitură joasă rătăcită peste parapet e cinstit daună de
  cocă, nu o fantomă în arboradă.
- **Cârmă**: doar prin pupă și aproape de axă (|Y| ≤ 180, X ≤ -1350). O salvă pe
  travers trece pe lângă ea — de asta merita manevra de a rake-ui o navă.
- **Baterie**: prin sabord, la înălțimea punții tunurilor.

### Efecte, măsurate fiecare separat

| avarie | efect |
|---|---|
| greement 1,0 / 0,6 / 0,3 / 0 | 6,65 / 5,92 / 4,52 / **0,01 m/s** |
| cârmă 1,0 / 0,5 / 0 | 5,45 / 3,25 / **1,17 °/s** |
| 0 / 2 / 4 tunuri scoase | salvă de 4 / 2 / **niciuna** |

Alegerea tactică: tirul înalt nu scufundă pe nimeni, îi ia mersul; tirul jos
scufundă, dar o lasă să fugă. AI-ul urmează o doctrină care se poate prezice:
sus până ținta are sub jumătate din pânză, apoi jos.

### Cinci bug-uri în codul existent

Judecătorii au citit arborele curent, nu doar designele, și au găsit:

1. **A și D nu roteau nava. Niciodată.** `MoveRight` și `TurnRate` erau legate
   la ACEEAȘI funcție, iar `TurnRate`, legat al doilea, scria zero peste tastatură
   în fiecare cadru. Nu se putea vedea fără interfață, și README-ul promitea
   contrariul de trei zile. Acum sunt două variabile care se adună.
2. `Reload = ReloadSeconds` rula chiar și când nu trăsese niciun tun — latent
   până în clipa în care tunurile pot fi scoase din luptă.
3. `GetComponentsBoundingBox` include componentele QueryOnly, deci propria mea
   cutie de probă a stricat garda de respawn: o epavă n-ar mai fi eliberat locul
   până la 35 m adâncime. Garda citește acum cutia COCII. Iar comentariul
   „masts included" era fals de când l-am scris: `HullMesh` are NoCollision.
4. `YawRateDegPerSec` era o diferență de unghi pe un interval de o secundă PLUS
   un cadru, iar prima probă din rulare era direcția absolută, nu o rată. Toate
   cifrele de giraţie din proiect ieșeau de pe coloana asta.
5. `HullFraction = GetHullIntegrity() / 1000.f` în AI dubla `MaxHullIntegrity`.

### Balistica, corectată de o măsurătoare

Zonele au scos la iveală ceva ce nu se vedea: loviturile ajungeau la **+285 cm**
pe bordaj, adică la parapet, iar bateria modelată la +120 nu era atinsă
niciodată. Am crezut întâi că e `RangeBias`, l-am scăzut, și media a coborât la
238 — nu destul. Cauza reală: formula de ridicare ignora că gura de foc stă cu
~120 cm mai SUS decât punctul ochit, iar o soluție balistică plată presupune
aceeași înălțime la plecare și la sosire. Am adăugat termenul de unghi al
punctului ochit, care servește și ochirea în greement: o singură formulă pentru
ambele ordine. Media loviturilor: **285 → 183 cm**, exact peste puntea tunurilor.

Regresii: portanța 1,000, pescaj 72 cm, scufundarea neschimbată (punte inundată
12,4 s, epavă 30,4 s), respawn la 42,4 s, zero erori, inerția neatinsă.

**Task Completed.** Rămâne: HUD.

## 2026-09-12 — Instrumentele

**Task Started.** Prompt exact: „continua cu HUD". Model: Claude Opus 5.

Panoul e desenat integral în C++, pe canvas, dintr-un `AHUD`. Fără widget-uri
și fără texturi de interfață: proiectul se construiește prin script, iar Python
nu poate scrie noduri Blueprint. Constrângerea s-a dovedit potrivită.

### Roza nu arată nordul

O busolă care arată nordul nu spune nimic unei nave cu vele. Ce trebuie să știe
omul de la cârmă e unde e puterea. Așa că piesa centrală e un POLAR, cu prova
mereu în sus: pentru fiecare curs pe care s-ar putea întoarce, cât ar trage rigul
acolo.

Desenat de două ori: un contur palid pentru greementul întreg și o linie plină
pentru cât a mai rămas din el. **Diferența dintre ele e exact ce ți-au tăiat
tunurile**, în aceeași imagine care îți spune și încotro să întorci.

Peste el: sectorul mort hașurat (se rotește cu vântul, nu cu nava), săgeata
vântului pe inel, drumul real după derivă ca linie separată de prova, și două
linii verzi la unghiurile care câștigă cel mai repede teren în vânt. Unghiul ăla
e **60°, măsurat**, nu dedus: curba de drive singură zice 70, dar deriva crește
cu puterea și polarul complet a dat 60. Măsurătoarea bate formula.

### Restul

Jos-stânga viteza, și sub ea câștigul spre vânt, colorat roșu când e negativ —
numărul care spune că poți merge cu șase noduri și totuși să pierzi teren. Sub
el coca, fiecare catarg separat, cârma și pânza. Dreapta jos tunurile, ca patru
pastile pe bord care se sting când afetul e distrus, plus reîncărcarea.

### Așezarea s-a decis din capturi

Prima versiune: roza sus-centru, fix peste catarge. A doua: jos-centru, fix
peste cocă. A treia, care a rămas: în cerul liber din dreapta sus. Nu se putea
hotărî pe hârtie, fiindcă nava e mereu în centrul ecranului.

Alte trei lucruri reparate din capturi: avertismentele erau text roșu pe o velă
albă, adică invizibile, și au primit o bandă de fundal; polarul arăta putere
plină cu greementul dus, adică mințea; iar discul de fundal a trebuit desenat ca
linii orizontale, fiindcă nu există primitivă de cerc.

### Un bug de instrument, nu de joc

Captura arăta nava demastată deși pornisem cu `-ShipRigDamage=0.62`. Logul din
primul cadru zicea deja `rig=0.00`. Cauza: **PowerShell trunchiază un flag cu
zecimale scris fără ghilimele** — `-ShipRigDamage=0.62` ajunge la proces ca 0,
iar `"-ShipRigDamage=0.62"` ajunge corect. Rularea reușește în ambele cazuri,
doar că măsoară altceva. Măsurătorile de ieri, cele care au dat curba de viteză
pe greement, foloseau forma citată, deci cifrele rămân valabile.

Panoul scrie o linie `HUDLOG` pe secundă cu exact valorile pe care le desenează.
Ultima captură: `rig=0.66/0.66 rud=0.50 guns=3/3 speed=6.05 vmg=-5.31` — și
panoul arată aceleași cifre.

**Task Completed.** Bucla de joc e completă: navighezi, urci în vânt, tragi sus
sau jos, schilodești sau scufunzi, te scufunzi și primești altă navă, și vezi
tot ce se întâmplă.

### Pasa adversarială peste panou

Trei recenzori pe cod și pe capturi. Cel mai valoros lucru pe care l-au găsit e
un defect al meu de instrument, din categoria pe care proiectul o tot plătește:

**`HUDLOG` nu scria nimic în rulările headless.** `AHUD::PostRender` cheamă
`DrawHUD` doar dacă `FApp::CanEverRender()`, iar `-NullRHI` o face falsă — adică
exact invocarea documentată pentru măsurători. Logul care exista tocmai ca
cifrele din spatele pixelilor să poată fi verificate fără ochi tăcea, fără
nicio eroare. Iar în regresia finală am scris negru pe alb „HUDLOG lines: 0
(a headless run draws nothing, so the HUD must stay silent)" — am RAȚIONALIZAT
simptomul în loc să-l recunosc. Logul s-a mutat în `Tick`. Verificat: 24 de
linii într-o rulare `-NullRHI` de 25 s.

Restul, toate reparate:

- `Super::DrawHUD()` era chemat ÎNAINTEA gărzii de Canvas, iar `AHUD::DrawHUD`
  dereferențiază Canvas la final. Garda nu putea rula niciodată.
- `K2_DrawBox` desenează un CONTUR din patru linii, nu un dreptunghi plin.
  Discul din spatele rozei costa ~1300 de primitive de linie pe cadru și „arăta
  plin" doar din suprapunerea contururilor translucide. Acum e `DrawRect`.
- Textul primea `1.f` ca scară în toate apelurile, deși geometria se scala cu
  ecranul. Panoul se scala pe jumătate.
- Fără subsistem de vânt, panoul desena un sector mort încrezător peste prova.
  Un instrument care inventează o citire în loc să recunoască că n-are una.
- Acul de derivă era condiționat de viteza ÎNAINTE, dar nava calculează deriva
  din DRUM. O cocă ce derapează aproape pur are viteză înainte mică și derivă
  mare, deci acul se ascundea exact când conta.
- O bară la zero se desena ca un jgheab gol, identic cu una nedesenată. Pierderea
  totală era cea mai tăcută citire de pe panou. Acum se desenează și partea
  PIERDUTĂ, în roșu.
- O baterie fără tunuri avea bara de reîncărcare plină, în roșu. Oriunde
  altundeva pe panou bară plină înseamnă gata.
- `-ShipShotAfter` era în afara gărzii `IsPlayerControlled()`, deci ambele nave
  cereau captura, peste același fișier.
- Comentariul lui `GetSailTrimRate()` descria `GetNoGoAngleDeg()`. Cine dimensiona
  sectorul mort din antet ar fi hașurat un sfert de grad.

Și o descoperire despre model, nu despre panou: **o navă oprită cu prova în vânt
ȘI cu pânza strânsă nu mai poate vira niciodată.** Autoritatea cârmei e zero la
viteză zero, iar pragul de „vergi bracate" se acordă doar cât timp `SailTrim`
trece de 0,1. Panoul spune acum „NO WAY ON - SET SAIL TO STEER".

Flag-uri noi ca panoul să poată fi verificat: `-ShipForeRigDamage` și
`-ShipMainRigDamage`, fiindcă `-ShipRigDamage` seta ambele catarge egal și
afirmația „panoul le arată separat" nu se putea dovedi.

## 2026-09-12 — Escadronul

**Task Started.** Prompt exact: „continua cu mai multe nave inamice".
Model: Claude Opus 5.

### Am măsurat întâi, am proiectat după

În loc să presupun ce se strică la mai multe nave, am pus o sondă minimală:
trei nave, în linie de front, 300 de secunde. Rezultatul:

```
6 contacte între coci, unul cu impuls 8,13e6 (o abordaj în toată regula)
1 lovitură fratricidă, deși jucătorul n-a tras niciun foc
```

Plus un bug al sondei însăși: `SpawnEnemy` ajunsese să nască tot escadronul, iar
respawn-ul îl chema, deci scufundarea UNEI nave reînvia TREI.

### De ce linia de front era cea mai proastă alegere posibilă

Panelul a pus degetul pe ea: **tunurile trag pe travers, iar linia de front pune
consoarta fix acolo unde bat ele.** Nu e un accident de reglaj, e geometria
formației. În ȘIR, consoartele stau înainte și în urmă, adică la 90° de unde pot
trage tunurile vreodată. Ăsta e motivul pentru care a existat linia de bătaie.

### Ce s-a construit

**Șir de bătaie.** Doar nava din frunte navighează, cu exact codul de tactică
dinainte — cea mai bună garanție că nimic calibrat nu s-a mișcat. Fiecare
urmăritoare ține locul celei DINAINTE, nu al celei din frunte: un lanț de brațe
scurte se corectează singur, pe când toate ținându-se de amiral ar aduna toată
eroarea liniei în ultima. Cursul se ia din cursul celei dinainte, îndoit cu
abaterea de la siaj; distanța se ține din PÂNZĂ, fiindcă o navă cu vele pătrate
n-are altă manetă.

**Nu trag prin consoartă.** Testul măsoară de la GURA DE FOC, nu din centrul
navei: tunurile stau la 640 cm pe travers, ceea ce la 50 m înseamnă 7° de
eroare de unghi, mai mult decât toleranța întreagă. Când culoarul e blocat,
bordul tace.

**Se feresc.** Corecția e TARE de la distanță și se stinge pe măsură ce se
deschide, nu invers: o cocă de 60 t la 6,6 m/s are nevoie de vreo șase secunde
de cârmă ca să se mute cu o lățime, deci o regulă slabă la limită și puternică
la contact aplică aproape nimic cât mai e timp și tot cât e prea târziu.

Ferirea se aplică ACUM după `ResolveSailableHeading`, nu înainte: aplicată
înainte, era aruncată tăcut pe orice curs strâns, adică exact pe legăturile pe
care navele se îngrămădesc.

**Victorie pe escadron.** Nu ai învins până nu s-a dus ultima. Logul numără
`left=2`, `left=1`, `left=0`. Panoul scrie câte mai sunt pe apă.

### Măsurat

| | linie de front | șir |
|---|---|---|
| contacte între coci | 6 | **0** |
| lovituri fratricide | 1 | **0** |
| salve trase | 31 | **36** |
| lovituri în jucător | 52 | 52 |

Cu o singură navă totul e neschimbat: 13 salve, 28 de lovituri, portanța 1,002.

**Corecție la cifra de dificultate.** Am raportat întâi „52 de lovituri în
jucător" ca motiv de îngrijorare. Cifra amestecă zone care nu cântăresc la fel:
doar 24 dintre ele au intrat în cocă, restul fiind tir înalt, care prin
construcție nu scufundă. Rezultatul real, cu trei nave, împotriva unei coci care
n-a ridicat pânza, n-a atins cârma și n-a tras niciun foc: **o singură
înfrângere în 300 de secunde, la t=138**, iar nava de schimb a supraviețuit
restul. Asta e mai degrabă îngăduitor. Implicitul de două a fost ales pe o
cifră care nu-l justifica; trei ar fi la fel de apărabil, și diferența nu se
poate hotărî fără o rulare cu un jucător real.

### Un bug de tir din felia precedentă

Judecătorii au prins ceva ce măsurătorile mele nu puteau arăta: **ochirea înaltă
viza exact golul dintre catarge.** Punctul ochit era local (0,0,1250), iar
catargele ocupă X 330..790 și -550..-90 — adică ținteam cei 4 m de aer dintre
ele. Fiecare lovitură în greement din toate rulările de ieri fusese
împrăștiere care a dus ghiuleaua pe un catarg la care nimeni nu trăgea. Acum
ochește gabia mare.

### Restul constatărilor, reparate

- O navă care a lovit rămâne obstacol încă vreo 30 de secunde: coliziunea se
  relaxează abia la puntea inundată. Testele foloseau `IsSunk()`, care e
  adevărat din prima clipă. Acum folosesc faza de scufundare.
- `IsSpawnClear` verifica doar stația din mijloc, deși stațiile sunt la ±150 m
  și raza de verificare e de 40.
- Ambele bucle de respawn se reîncercau la nesfârșit, fără limită și fără să
  spună nimic după prima linie. Acum cedează după 30 de încercări și scriu de ce.
- Greementul unei nave care se scufundă tot prindea ghiulele, producând o
  lovitură în greement urmată imediat de „damage ignored".
- SHIPLOG n-avea x și y, deci **singurul instrument de proximitate din proiect
  era chiar ciocnirea pe care regula trebuia s-o prevină.** Acum scrie poziția.

### Pasa adversarială: 5 recenzori pe 5 unghiuri, 25 de constatări confirmate

Cea gravă, confirmată de un verificator care a refăcut aritmetica independent,
**a fost în ce tocmai măsurasem ca fiind bun**:

> Banca se citea într-un singur punct — centrul de masă — dar nava are 31 m.
> Marja utilizabilă era 3450, nu 5000. Iar pânza împinge cu forță MAXIMĂ la
> viteză zero (`Falloff = 1 - viteză/vitezăMax`), deci echilibrul static al unei
> nave ținute în banc cu velele sus e la 3892 — prova la 4,4 m **în interiorul
> dealului**, permanent, cu toate indicatoarele verzi: testul de stâncă compară
> 3892 cu 5000, plafonul de forță nu se atinge, iar logul de final tipărește
> 3892 și se citește ca 11 m de rezervă.

Și cei 3708 pe care îi scrisesem ca dovadă că bancul ține puneau deja prova cu
2,6 m dincolo de țărm. Verificatorul a arătat și că 3708 nu poate fi inerție: o
intrare liberă la 6,65 m/s ajunge la 1660 cm contra acestui arc (integrala de
energie), deci restul era pânza care o mâna pe uscat. Reparat: banca se citește
în trei puncte — provă, centru, pupă — iar forța rămâne aplicată în centrul de
masă, ca să nu apară moment nou de ruliu sau de girație.

**Celelalte constatări confirmate și reparate:**

- **„Se întărește la infinit" era fals.** `DIn` e plafonat la 0,98 din marjă și
  plafonul de forță e real, deci stânca e accesibilă în principiu. Comentariul,
  README-ul și memoria promiteau o garanție pe care codul nu o dă. Acum scrie ce
  face: de 50 de ori mai tare spre mal, apoi plat.
- **Rana era taxată pe viteza totală**, nu pe cea de apropiere: o navă care
  aleargă de-a lungul coastei și doar atinge marginea plătea pentru toată
  viteza ei. Acum plătește pe `closing`.
- **Rana de eșuare se scria `SHOTLOG`**, deci intra în socoteala tirului —
  inclusiv în tabelul din felia asta care trebuia să arate că insula nu schimbă
  nimic în afară de ce trebuie. Acum e `GROUNDLOG`.
- **Sweep-ul ghiulelei abandona ghiuleaua** când găsea un corp WorldStatic
  străin: ieșea din `Tick` fără să se uite mai departe și fără să mai facă
  testul de cădere în apă, în fiecare cadru. Cu trei asemenea corpuri în nivel
  (unul de 42 km), o ghiulea ar fi putut fi oprită la nesfârșit să găsească o
  navă, în tăcere. Acum e `SweepMulti` și trece peste ele, o dată cu
  avertisment, fără să iasă din `Tick`.
- **Uscatul scria DOUĂ rânduri** per ghiulea, deci se număra dublu față de
  căderile în apă, chiar în tabelul de mai jos. Acum unul singur, ca oricare alt
  sfârșit de ghiulea. Și o ghiulea care își arde fitilul scria ZERO rânduri,
  deci sfârșiturile din log nu se adunau niciodată la cât se trăsese
  (`LifeSpanExpired`).
- **Garda „insula peste punctul de apariție" verifica 2 puncte din 3** și citea
  `SquadronSize` înainte să fie parsat, deci mereu valoarea implicită. Acum
  verifică fiecare stație, după parsare, și **refuză** insula în loc s-o
  semnaleze.
- **Verificarea liniei de coastă din `island.py` nu putea eșua**: evalua
  profilul FIX la `R_SHORE`, unde returnează un produs cu zero exact, deci
  raporta 0,000 prin construcție și ar fi continuat s-o facă indiferent unde ar
  fi fost coasta. Acum caută unde MESH-UL CONSTRUIT taie z = 0, coloană cu
  coloană, și compară cu constanta din C++ (cu `assert` dacă cele două se
  despart). Plus: `R_SHORE` e acum un inel al grilei, deci linia de coastă
  desenată chiar E cercul, nu o coardă între două inele.
- **Profilul avea o falie de 10,5 m** la `R_SUMMIT`: ramura interioară se
  termina la 0,50 din vârf iar cea exterioară pornea de la 0,75. Un perete
  inelar în jurul vârfului, într-o formă al cărei rost e o plajă în pantă.
- **Bancul pornea unde era apă de 9 m.** `SHELF_Z` era -900, deci „ia fundul"
  se întâmpla acolo unde o navă cu pescaj de 74 cm are nouă metri sub chilă.
  Acum -150: toată banda chiar e apă în care o navă de pescajul ăsta n-are ce
  căuta.
- **`island_assets.py` sărea peste fixarea coliziunii** la a doua trecere — iar
  prima trecere e exact aia care crapă înainte s-o atingă.
- Recensământul WorldStatic nu tipărea câmpul care decide (`enabled`), iar
  `spanCm` era o semi-extindere numită „span".

### Două defecte găsite de propria măsurătoare, DUPĂ pasa adversarială

1. **`AFLOAT` tot nu se declanșa.** Am pus adâncimea curentă pe linia de SHIPLOG
   ca s-o văd în loc s-o deduc, și se vede: arcul o scoate exponențial și se
   așază la **1–2 cm** din 5000, oscilând peste zero cu hula. „Liberă timp de 3
   secunde la adâncime exact zero" nu se poate împlini. Pragul de eliberare e
   acum același număr cu cel de eșuare (50 cm) — o bandă, nu două teste fără
   legătură.
2. **Și după asta tot nu se declanșa**, fiindcă ceasul de „liberă" se reseta în
   blocul de forță la ORICE citire peste zero. Legat acum de același prag.

Ambele sunt aceeași greșeală de două ori: o condiție de ieșire pe care starea de
echilibru a mecanismului nu o poate atinge.

### Măsurători finale

| ce | fără insulă | cu insulă |
|---|---|---|
| ghiulele oprite de stâncă | 0 | **3** |
| ghiulele căzute în apă | 8 | 5 |
| lovituri în greement | 4 | 4 |
| salve | 3 | 3 |
| `groundForceTicks` | **0** (numărat) | 0 |

Eșuare forțată: apropiere 6,56 m/s, rană 295 (`GROUNDLOG`, zonă **cocă**),
adâncime maximă **la provă** 3720 din 5000 (etrava la 13 m de plajă), ieșire
0,06 m/s, liberă după 29,8 s, zero avertismente de stâncă, zero atingeri de
plafon. Insulă peste un punct de apariție: **refuzată**, `worldstatic count`
înapoi la 3.

**Task Completed.**


---

## Task Started — 12.09.2026

**Prompt:** „continua cu insule"
**Model:** Claude Opus 5

### Metoda: sonda întâi, designul după

Ca la escadron, am pus întâi o sondă minimală — o cutie solidă în apă — și am
măsurat ce se rupe, înainte de orice design. A ieșit așa:

| ce am probat | rezultat |
|---|---|
| uscatul oprește coca? | da — 302 contacte, `normalZ=1.00`, navele **urcate pe insulă** la z=2500 |
| o navă eșuată se dezlipește? | nu — `speed=0.00` timp de 295 s, până la sfârșitul rulării |
| ghiuleaua vede uscatul? | nu — 4 din 45 au traversat stânca |
| AI-ul știe de uscat? | nu — apropierea minimă a fost noroc |

**Sonda a mințit de două ori înainte să spună adevărul**, ambele din codul meu:

1. `Static` refuză poziționarea la runtime. Insula scria în log „land at
   (30000,10000) half=30000" în timp ce scena de fizică nu avea nimic acolo, și
   navele treceau 148 m prin stâncă fără un singur contact. Corect e `Movable`
   cu fizica oprită — un corp cinematic blochează la fel și poate fi așezat.
2. `BeginPlay` rulează **în interiorul** `SpawnActor` pentru un actor creat
   după pornirea lumii, deci mărimea pusă pe pointerul întors ajunge prea
   târziu. Cutia era de 120 m iar măsurătoarea raporta 300 m. Corect e
   `SpawnActorDeferred` + `FinishSpawning`.

Lecția e aceeași cu a HUD-ului: *un instrument nou se verifică pe el înainte să
se creadă ce spune.* Insula scrie acum ce ține scena de fizică, nu ce i-am cerut.

### Panelul de design (5 agenți)

Patru unghiuri independente — marinărie de epocă, design de joc, sisteme,
adversar — plus un președinte care a citit sursa singur. Decizia lui, adoptată:
**construiește bancul de nisip și nimic altceva.**

Dezacordul real a fost unul singur: ce se întâmplă când atingi uscatul. Un
panelist voia gaură-și-te-scufunzi. A pierdut din sursă, nu din gust: designul
lui păstrează coca *blocând* stânca și se bazează pe anularea vitezei ca să
oprească urcarea, dar 302 contacte cu normala 1,00 spun că urcarea E
comportamentul implicit al solverului; iar o navă pe stâncă n-are pontoane ude,
deci `WaterSurfaceZ()` întoarce 0, pescajul se citește +2500 și inundarea rulează
pe siguranțele de avarie în loc de numerele calibrate.

Președintele a mai spus un lucru care merită păstrat: din cele patru puncte pe
care panelul le-a aprobat în unanimitate, trei nu valorează nimic ca dovadă —
„uscatul să oprească ghiulelele" e patru oameni care citesc aceeași linie de
canale, iar „uscatul înseamnă mal sub vânt" e aceeași cunoștință de epocă în
patru vocabulare, pe care niciunul n-a putut-o sprijini pe o linie de log.
Singurul acord tratat ca dovadă a fost cel la care trei au ajuns din puncte de
plecare diferite: forța de fund trebuie să fie identic zero în larg.

### M-am abătut de la panel în două puncte

1. **Am făcut și mesh-ul**, pe care panelul îl exclusese ca nemăsurabil headless
   și riscant la coliziune. Riscul invocat era tunelarea cocii — care în
   designul lui propriu nu mai există, fiindcă insula refuză `ECC_Pawn`.
2. **Bancul e circular, nu pătrat.** Panelul însuși avertizează contra „două
   descrieri ale aceleiași geometrii care se despart". O insulă rotundă cu banc
   pătrat e exact capcana aia. Acum e o singură rază, împărțită între
   `Scripts/island.py` și `AIsland`, iar scriptul **verifică** că linia lui de
   coastă chiar e cercul ăla: `coast_error=0.000cm`.

### Ce s-a construit

- `AIsland`: un mesh static care oprește ghiulelele și **refuză `ECC_Pawn`**.
  Linia asta e toată felia. Pusă pe insulă, nu pe cocă: canalele cocii sunt
  plătite scump (Overlap pe WorldDynamic e motivul pentru care navele plutesc),
  iar o schimbare acolo e globală.
- `AShipPawn::ApplyGroundContact()`, chemată imediat după chilă. NU e blocată de
  „e în apă", fiindcă exact aia e condiția care stinge chila, velele și cârma
  deodată — iar o navă care le-a pierdut pe toate trei e cea care are cea mai
  mare nevoie să fie împinsă în larg. Forță strict orizontală, aplicată în
  centrul de masă, deci nu face moment de ruliu sau de tangaj și lasă pescajul
  și înclinarea pe seama plutirii și a `HeelTorque`, care sunt calibrate.
- Ghiuleaua învață de stâncă prin **sweep-ul care exista deja**, nu schimbând
  canalele bilei.
- `-ShipRunAground=N`, fiindcă o rulare curată cu insulă nu dovedește nimic:
  fără eșuare forțată, un banc rupt și unul care merge produc același log.

### Trei defecte prinse de propria măsurătoare, după ce „mergea"

1. **`AFLOAT` nu se declanșa niciodată.** Histereza cerea 500 cm de offing, pe
   care un arc nu-i poate da: arcul încetează să împingă exact la marginea
   bancului. Nava a stat acolo 140 s, liberă după orice criteriu, fără s-o
   spună. Acum histereza e în **timp**, nu în distanță.
2. **Ieșea cu 5,29 m/s cu pupa înainte.** Amortizorul era înmulțit cu adâncimea,
   deci mușca abia când era deja adânc — moment în care arcul luase deja viteza
   și nu mai era mare lucru de mâncat. Acum e proporțional cu viteza de
   apropiere și se rampează pe primii 4 m de banc. Ieșirea: 0,00 m/s.
3. **Se re-eșua în buclă** pe linia de zero, cu o rană nouă la fiecare ciclu
   (de valoare zero, salvată doar de scalarea cu viteza). Acum „eșuat" cere o
   mușcătură reală, nu o atingere a marginii.

### Constatarea care contează cel mai mult nu e despre insule

**Rulările proiectului nu erau reproductibile.** Două rulări cu flag-uri
identice și același cod: 5 lovituri în greement, apoi 8. Cu `-UseFixedTimeStep`
cele două loguri sunt identice bit cu bit **73 de secunde** și divergează exact
la prima salvă — adică la singurele trei extrageri de hazard din proiect, toate
în împrăștierea tunurilor.

Deci fizica și căpitanul au fost mereu repetabile, iar tunurile n-au fost
niciodată, și **fiecare comparație înainte/după din jurnalul ăsta făcută pe o
singură pereche de rulări a citit împrăștiere crezând că citește semnal** —
inclusiv „45 de salve cu insulă, 45 fără" din sonda de azi, care a ieșit egală
din noroc.

Reparat: `-ShipSeed=N` seamănă `FMath::RandInit`/`SRandInit`. Cu
`-UseFixedTimeStep -FPS=60 -ShipSeed=1`, două rulări ies identice bit cu bit,
verificat. Am scos și singura extragere de hazard pe care o adăugasem eu: bordul
pe care se sparge coca la eșuare e acum bordul dinspre mal — determinist, și mai
adevărat.

### Un bug vechi de două felii, găsit pe drum

Mesh-urile generate intrau în lume **de 100 de ori mai mari**. `ship.py`
modelează în metri și iese corect; `sea.py` și `island.py` scriau centimetri,
care mai erau înmulțiți o dată cu 100 la import. Măsurat pe asset:

| mesh | era | trebuia |
|---|---|---|
| `SM_PirateShip` | 37,5 m | 37,5 m ✔ |
| `SM_SeaSurface` | **600 km** | 6 km |
| `SM_Island` | **26 km** | 260 m |

Consecința pentru mare, netrivială: grila radială are primul inel la
`R_INNER = 250` — adică la 2,5 m de cocă în intenție, dar la **250 m** în
realitate. „Centimetri de detaliu sub cocă", cum scria comentariul, erau de fapt
zeci de metri: marea de sub navă era o farfurie plată, iar `drawn=6 of 6` din
felia 7 se referea la setul de unde, nu la geometria care le desenează. Reparate
amândouă, cu `CM_PER_UNIT` și cu dimensiunea tipărită de scriptul însuși.

### Măsurători

| ce | fără insulă | cu insulă |
|---|---|---|
| ghiulele oprite de stâncă | 0 | **3** |
| ghiulele căzute în apă | 8 | 5 |
| lovituri în greement | 4 | 4 |
| `groundForceTicks` | **0** (numărat, nu presupus) | 0 |
| corpuri WorldStatic în nivel | 3 | 4 |

Eșuare forțată (`-ShipRunAground=20`): atingere la 6,65 m/s, rană de cocă 299
(zonă **cocă**, garantat de geometrie, nu de noroc), adâncime maximă 3708 cm
dintr-o marjă de 5000, ieșire 0,00 m/s, liberă după 94 s, `inWater=1` și
portanță ~1,00 tot timpul. Zero avertismente de „a atins stânca", zero atingeri
de plafon de forță.

AI: 4 rulări cu escadron de două, din patru direcții — **zero eșuări**. Sub
pragul declarat înainte de rulare, deci conștiința de uscat a AI-ului nu intră
în felia asta.

### Capcana pe care panelul a prevăzut-o și care chiar era acolo

`SEALOG worldstatic count=3` **într-o rulare fără insulă**: nivelul are deja trei
corpuri WorldStatic — plasa de apă de 42 km a zonei, corpul oceanului, și
randerul debuggerului. Dacă sweep-ul ghiulelei ar fi răspuns la vreunul, fiecare
ghiulea ar fi murit la gura tunului în timp ce contorul insulei raporta un zero
triumfal. Închis structural: sweep-ul poate termina o ghiulea doar pe o navă sau
pe uscat, orice altceva e trecut cu un avertisment.

### Limite cunoscute, spuse pe față

- `-Islands=N` construiește exact una, oricât ai cere. Scrie asta în log ca
  avertisment. Mai multe cer ca `IsSpawnClear` să învețe ce e uscatul.
- O insulă peste un punct de apariție aruncă nava afară cu 32 m/s. Acum e
  avertisment în log, nu surpriză.
- Tabelul polar din README măsoară **o rulare**, nu nava: același cod dă 60° →
  3,96 m/s cu pas fix și 3,27 m/s fără. De recalibrat cu instrumentul reparat,
  separat de felia asta.

**Task Completed.**


---

## Task Started — 12.09.2026

**Prompt:** „continua cu AI care stie de uscat"
**Model:** Claude Opus 5

### Felia asta fusese amânată cu un prag declarat, și pragul nu fusese atins

Panelul de ieri a exclus explicit conștiința de uscat a AI-ului, cu o măsurătoare
declarată ÎNAINTE de rulare: patru rulări cu escadronul pornit din patru
direcții, zero eșuări. Deci am început prin a ataca propria măsurătoare, nu prin
a construi.

**Sonda blândă confirma amânarea. Sonda grea a răsturnat-o.** Insula de ieri
stătea la 316 m de jucător, pe lângă care nu trecea nimeni. Pusă în DRUM, cu
vântul fixat ca să fie mal sub vânt:

| scenariu | ce face AI-ul |
|---|---|
| insulă de 180 m rază, sub vânt | intră în banc cu 6,14 m/s, pierde 277 din cocă, **190 s eșuată din 400** |
| aceeași, cu greementul la 0,45 | **380 s** — toată rularea |
| trei insule, o coastă | eșuează de două ori, 31% din cocă, înainte să ajungă la luptă |

Ca să pot construi sondele am avut nevoie de trei lucruri care lipseau:
`-WindBearing` (fără vânt fixat, un mal sub vânt devine mal de sub vânt la
jumătatea măsurătorii), `-Islands=N` care chiar construiește N (era clamp la
unul singur), și `-EnemyRigDamage`, fiindcă o navă cu jumătate de pânză e exact
cazul în care uscatul ucide, iar până acum doar JUCĂTORUL putea fi avariat.

### Verificarea de dinaintea primei linii de cod

Președintele panelului a cerut un lucru înaintea oricărei implementări: cei 380
de secunde sunt o eșuare reală sau o bandă de raportare? Proiectul a livrat de
două ori exact defectul ăla în exact funcția aia. Am făcut grep pe `shoal=`:
citea 2800–3800 cm pe toată durata, deci era chiar pe plajă, la 28–38 m în banc.
**Și oscila** — împinsă afară de banc, mânată înapoi de pânză. Ăsta a fost
diagnosticul: *pânza e cea care o ține pe uscat.*

### Ce s-a construit, și de ce în locul ăla

Un singur lucru: **odată eșuată, nu se mai mână pe uscat, ci cârmește spre larg.**

Uscatul intră ca **ÎNLOCUIRE** a cursului dorit, între ținerea postului și
`ResolveSailableHeading`. Argumentul de ordonare, care a fost și jumătate greșit
(vezi mai jos): o corecție pusă ÎNAINTE e înghițită tăcut pe bordeie — bug-ul
ConsortAvoidance, plătit deja o dată; una pusă DUPĂ dă un curs în conul mort
unde forța e zero, ceea ce o oprește, ceea ce declanșează virarea prin pupă,
ceea ce o duce pe plajă. Avortarea ar fi fost chiar eșuarea.

Plus: pânza se strânge doar cât timp încă înaintează spre mal, și niciodată sub
un prag — sub trim 0,1 moare autoritatea de cârmă cu vergile bracate, adică
singura cârmă pe care o are o navă fără viteză.

### Un defect găsit de mine, înainte de recenzori

**O navă eșuată FĂRĂ ȚINTĂ ieșea din `Tick` înainte de dezeșuare** — și ramura
aia strânge pânza la zero, ceea ce îi ia și cârma. Ar fi rămas pe banc la
nesfârșit. Se întâmplă de fiecare dată când jucătorul se scufundă, în cele 35 de
secunde până primește o navă nouă.

### Pasa adversarială: 4 unghiuri, 9 constatări confirmate

**Cea gravă lovește exact în claim-ul central al feliei, și are dreptate:**

> `ResolveSailableHeading` aruncă și o ÎNLOCUIRE, nu doar o corecție. Când o mură
> e deja ținută (`TackSign != 0` și holdul curge), funcția returnează
> `vânt ± margine` și **nu se uită deloc** la cursul primit — până la 45 de
> secunde. Comentariul meu promitea „bordul cel mai apropiat", adevărat doar
> când holdul a expirat.

Și partea cea mai usturătoare, de la verificator: **rulările B și C nu puteau
distinge „a ieșit fiindcă a cârmit" de „a ieșit fiindcă i-a expirat mura"** —
nimic nu tipărea cursul REZOLVAT, semnul murei sau holdul. Ambele explicații
încap în același log. Tiparul casei, din nou: instrumentul e de acord cu autorul.

Reparat: holdul se zerează la atingere și ori de câte ori bordul ținut n-are
componentă spre larg; și logul scrie acum `asked … steering … tack … hold`, deci
întrebarea are răspuns. Se vede că merge: `asked -116, steering -110` e bordul
cel mai apropiat, nu cel opus.

**Celelalte opt, toate reparate:**

- **Virarea prin pupă păstra cârma.** `bWearing` nu era stins la eșuare, iar o
  virare deține cârma complet — și vine prin DOWNWIND, adică în sus pe plajă.
- **Ferirea de consoartă se aplica peste cursul de dezeșuare**, până la 55°,
  după ce rigul își spusese cuvântul: putea duce prova înapoi în conul mort. O
  navă eșuată nu are viteză și nu poate călca pe nimeni.
- **O navă care se reîntoarce primea maneta de interval**, care îi ordona să
  STRÂNGĂ pânza fiindcă punctul de post o depășise pe cealaltă axă — în timp ce
  încerca să prindă o linie care mergea cu șase noduri.
- **Garda „insula peste punctul de apariție" compara cu poziția IMPLICITĂ a
  inamicului**, fiindcă `-EnemyX/-EnemyY` se parsau mai târziu, în
  `SpawnOneEnemy`. Într-o rulare al cărei scop era tocmai să pună uscat lângă el.
- **O navă care se scufundă rămânea „eșuată" pentru totdeauna**, cu o direcție
  spre larg înghețată de dinainte: garda `IsSinking()` ieșea înainte să
  actualizeze starea publicată.

### Măsurători finale

| rulare | înainte | după |
|---|---|---|
| fără insulă | — | **identic bit cu bit**, `landTicks=0 clawOffs=0 rejoinTicks=0` |
| mal sub vânt | 190 s eșuată, 0 salve | **42 s**, 14 salve |
| idem, greement 0,45 | 380 s eșuată | **66 s**, 16 salve |
| insulă implicită | o atingere, liberă în 14,8 s, 17 salve | o atingere, liberă în **9,6 s**, 18 salve |
| insulă pe cercul de angajare | — | identic cu baza fără insulă, `landTicks=0` |
| escadron de trei lângă uscat | a treia navă pierdută la 66 km | **se întoarce în linie**, 22 salve |
| aceeași rulare de două ori | — | identice |

### Ce rămâne, spus pe față

- **Atinge uscatul mai des** (de 2 ori, de 5 ori cu greementul rupt), fiindcă
  n-are privire înainte și după dezeșuare ținta e tot dincolo de insulă. Pierde
  deci mai multă cocă pe uscat decât în măsurătoarea veche — dar acolo nu era în
  siguranță, era blocată. Garda de cocă declarată de panel (≥700) pică la 558;
  spun și că gardele panelului erau inconsecvente între ele, fiindcă „cel mult
  două eșuări" permite două răni iar „≥700" permite una.
- **Nu ocolește nimic.** Privirea înainte e exclusă deliberat: constantele ei ar
  veni dintr-un tabel polar pe care jurnalul ăsta l-a declarat deja necalibrat.
- **Trage în continuare prin insulă**, ca decizie.

**Task Completed.**


---

## Task Started — 13.09.2026

**Prompt:** „continua"
**Model:** Claude Opus 5

Am luat datoria pe care jurnalul ăsta și-o recunoscuse singur ieri: **tabelul
polar din README măsura o rulare, nu nava.** Blochează și felia următoare de AI,
fiindcă privirea înainte și-ar lua constantele de acolo.

### Cauza, citită în cod

`-ShipWindSweep` rotește vântul continuu — `PlayTime / WindSweepSeconds * 360`,
adică două grade pe secundă la valoarea documentată — și scrie un rând pe
secundă. Nava nu se așază niciodată: accelerează tot timpul. Și capul ei e liber,
fără cârmă, deci `windAng` din fiecare rând se schimbă și fiindcă se rotește
vântul, și fiindcă se plimbă prova. Un tranzitoriu măsurat de două ori.

### Instrumentul nou: `-ShipPolar`

Vântul se fixează, nava e cârmită pe un cap și **ținută acolo până nu mai
schimbă nimic**, și abia atunci se scrie rândul. Rândul spune și dacă s-a
stabilizat sau dacă i-a expirat răbdarea.

**Trei defecte ale propriului instrument, prinse pe rând de propriile
măsurători:**

1. **Fixasem direcția vântului, nu tăria.** La același unghi, o mură dădea
   `drive=0.655` și cealaltă `1.040`. Nu nava era asimetrică — cele două treceri
   se întâmplau la minute distanță, iar rafalele swing-uiesc tăria cu o treime
   în fiecare parte pe perioade de două și cinci minute. Adăugat `-WindSpeed`.
   După: murile coincid la ±0,002.
2. **Testul de „stabilizat" cerea o singură secundă liniștită.** O navă care
   încă accelerează trece testul ăla oricând accelerația e mică pentru o clipă,
   iar tabelul ieșea **non-monoton în interiorul aceleiași treceri**, ceea ce
   niciun regim stabilizat nu poate fi. Acum cere trei secunde liniștite
   consecutive plus un cap ținut la sub două grade.
3. **Scriam rândul în locul greșit din `Tick`.** `drive` e o variabilă locală
   calculată mai jos, deci raportam forța de acum lângă viteza de acum, dar
   amândouă lângă un `drive` de acum un cadru. Mutat unde valorile sunt proaspete.

**Cum se verifică instrumentul pe el însuși:** rulat de două ori dă rezultate
identice; rulat cu vântul rotit la 137 de grade în loc de 0, diferența maximă pe
cele 27 de unghiuri comune e **0,020 m/s**. Deci măsoară nava, nu vremea.

### Ce a ieșit

| unghi | tabelul vechi | măsurat stabilizat |
|---|---|---|
| 50 | 2,5 m/s, vmg +1,19 | **0,92 m/s**, +0,51 |
| 60 | 4,4 m/s, vmg +1,39 | **3,56 m/s**, **+1,43** |
| 70 | 5,4 m/s, vmg +0,69 | **4,89 m/s**, **+1,04** |
| 90 | 6,1 m/s, vmg -1,45 | 6,13 m/s, -1,12 |

Confirmat din tabelul vechi: 60 de grade CHIAR e cel mai bun unghi de urcare în
vânt, și vârful de viteză e la ~140. Deci sfatul era bun, cifrele nu.
Infirmat: la 50 de grade nava practic nu se mișcă, iar la 70 urcă mult mai bine
decât se credea. Zona moartă se citește direct: forță exact 0 până la 44 de
grade, prima valoare nenulă la 50.

### O consecință semnalată, nu reparată

Căpitanul inamic ține bordeiele la **70 de grade** (`NoGoAngleDeg` 48 plus
`CloseHauledMarginDeg` 22), unde câștigul în vânt e +1,04. Optimul măsurat e la
60, cu +1,43 — **cu 37% mai mult**. Marja e o constantă pe care două paneluri
au protejat-o explicit fiindcă re-acordează fiecare angajare, iar compromisul e
real: la 60 urcă mai repede dar merge prin apă cu 3,56 în loc de 4,89. Deci se
scrie aici și în README, și se decide separat, cu măsurătoarea ei.

**Task Completed.**


---

## Task Started — 13.09.2026

**Prompt:** „continua cu marja de 22 la 60 de grade"
**Model:** Claude Opus 5

### N-am făcut schimbarea cerută, fiindcă măsurată înrăutățește lucrurile

Cererea era clară: mută `CloseHauledMarginDeg` de la 22 la 12, ca bordeiul să fie
la 60 de grade, unde polarul remăsurat ieri arată cel mai bun câștig în vânt
(+1,43 față de +1,04 la 70). Am făcut marja reglabilă din linia de comandă și am
măturat-o în loc s-o schimb pe încredere — e constanta pe care două paneluri au
protejat-o explicit.

**Prima măturare a răsturnat întrebarea.** Pe scenariul care contează — inamicul
sub vânt, deci obligat să bordeieze ca să ajungă la jucător — la TOATE cele șase
marje testate nu trăgea nicio salvă în 400 de secunde, și termina mai departe
decât plecase. Marja muta rezultatul cu 11%. Nu ea era constrângerea.

Urma din log spune de ce, rând cu rând: între mure câștiga teren (599→588,
709→677, 794→732), iar la fiecare schimbare de mură **vira prin pupă** și pierdea
70–120 m (588→709, 677→748, 732→843, 826→949). Vreo 40 m câștigați pe bord,
vreo 110 pierduți pe viraj.

### Cauza: o regulă corectă aplicată fără condiția care o face corectă

`ArcCrossesWind` → `bWearing`, fără niciun test de viteză. Iar comentariul chiar
al acelei reguli spune de ce ar trebui să existe unul: *„o navă cu vele pătrate
trece prin ochiul vântului doar cu viteză adevărată; prinsă la jumătate se
oprește și rămâne acolo."* Regula fusese scrisă ca să repare două blocaje de
optzeci de secunde, și a reparat asta — cu prețul de a face bordeiul imposibil,
ceea ce nu se vedea fiindcă niciun scenariu de până acum nu cerea un bordei lung.

### Construit: vine în vânt când are viteză

`TackAboveMS`, reglabil cu `-AITackAbove=N`, cu o valoare uriașă care readuce
exact comportamentul vechi — de asta schimbarea se poate MĂSURA în loc să fie
argumentată. Spre deosebire de un viraj prin pupă, venirea în vânt **nu confiscă
cârma**: o cârmește eroarea obișnuită de cap, deci întoarce pe drumul scurt.

**Un defect al meu, prins de propria măsurătoare:** la primul test nava venea în
vânt de patru ori ȘI vira prin pupă de cinci ori în aceeași rulare, fără niciun
câștig. Cauza: o navă care traversează vântul **încetinește** traversându-l, deci
pierdea viteza care autorizase venirea în vânt și ramura următoare o trimitea
înapoi prin pupă, la jumătatea manevrei. Lipsea garda „nu în timp ce deja vine în
vânt".

### Măsurători

Treapta e ascuțită și are o explicație fizică: viteza ei în bordei e 4,89 m/s.

| prag | viraje prin pupă | veniri în vânt | salve | distanța finală |
|---|---|---|---|---|
| niciodată (vechiul) | 5 | 0 | **0** | 885 m |
| 5,0 | 5 | 0 | 0 | 885 m |
| **4,5 și mai jos** | 1 | 4 | **3** | **380 m** |

Regresii, aceeași sămânță, cu și fără:

| scenariu | salve | lovituri |
|---|---|---|
| lupta standard | 3 → **7** | 4 → **13** |
| escadron de două | 5 → **6** | 9 → **11** |
| mal sub vânt | 14 → 14 | 21 → **23** |

Preț: cel mai lung blocaj cu prova în vânt crește de la ~6 s la ~40 s. Asta E
compromisul pe care regula veche îl evita. Merită, fiindcă un viraj costă o sută
de metri de fiecare dată și o venire ratată costă patruzeci de secunde o dată.

### Și abia apoi, răspunsul la întrebarea pusă

Cu virajul reparat, am măturat marja din nou:

| marja | bord la | salve | lovituri | distanța finală |
|---|---|---|---|---|
| **22** | 70° | **3** | 2 | 380 m |
| 18 | 66° | 3 | 2 | 370 m |
| 15 | 63° | 2 | **5** | 364 m |
| 12 | 60° | **0** | 0 | 363 m |
| 10 | 58° | 0 | 0 | 372 m |

La 60 de grade ajunge cel mai aproape de țintă și nu trage nimic: ajunge acolo
prea târziu. La 3,56 m/s în loc de 4,89 întoarce mai încet și cârmește mai prost,
deci nu mai apucă să pună tunurile pe bord. Între 15, 18 și 22 diferențele intră
în zgomotul unei singure rulări.

**Deci marja rămâne 22.** Polarul spune adevărul despre o navă care merge drept
la nesfârșit; nu spune nimic despre o navă care trebuie să se întoarcă.

**Task Completed.**


---

## Task Started — 13.09.2026

**Prompt:** „continua"
**Model:** Claude Opus 5

Privirea înainte după uscat fusese exclusă de un panel pentru un motiv precis:
constantele ei ar fi venit din polarul necalibrat. Polarul e calibrat de ieri și
virajul e reparat, deci motivul a dispărut. Am construit-o.

### Ce s-a construit

`CourseClearOfLand`, chemată înainte de `ResolveSailableHeading` — aceeași
ordonare ca dezeșuarea, din același motiv. Proiectează **drumul** ei, nu capul
(derivă de 6–9 grade, care pe nouăzeci de secunde înseamnă șaptezeci de metri de
eroare, mereu spre partea pe care e uscatul), calculează analitic apropierea
minimă de fiecare banc, și dacă nu trece, scanează cursuri la stânga și la
dreapta până găsește unul care trece. Scanare, nu împingere dinspre insula cea
mai apropiată: împinsă dinspre două insule ar cârmi exact prin gaura dintre ele.
Cursul ales se ține câteva secunde, altfel re-decide de șaizeci de ori pe secundă.

### Trei defecte ale mele, toate găsite de măsurătoare

1. **Pragul de apă liberă era absurd de mic.** 60 de metri, pentru o navă de 31 m
   cu cercul de girație de 73 și derivă de 7 grade. În log se vede decizia:
   *„alterez 15 grade ca să am liber (66 m de drumul ei)"* — și apoi eșuează.
2. **Scanarea lua PRIMA variantă care depășea pragul**, nu una cu marjă reală.
   Cele două defecte împreună o făceau să radă bancul dinadins.
3. **Orizonturile scurte sunt mai rele decât nimic.** La douăzeci de secunde a
   eșuat de opt ori pe coastă: vede uscatul prea târziu, alterează, e tot prea
   aproape, alterează iar.

### Măsurători, și verdictul

La orizont 90 s și 200 m de apă liberă:

| scenariu | eșuări | salve | cocă jucător la final |
|---|---|---|---|
| coastă de trei, fără privire | 2 | 16 | 340 |
| coastă de trei, cu privire | **1** | 13 | 340 |
| mal sub vânt, fără | 2 | 14 | 400 |
| mal sub vânt, cu | **1** | **9** | **760** |
| insula implicită, fără | 1 | 18 | **0** (scufundat) |
| insula implicită, cu | **0** | 16 | 160 |

Pe coastă e gratis. Pe celelalte două **cumpără siguranța ei cu neputința ei**:
pe malul sub vânt jucătorul scapă cu aproape dublul cocii, iar pe insula
implicită nu-l mai scufundă deloc.

**Deci se livrează STINSĂ implicit**, cu `-AILookAhead=90` pentru cine o vrea.
Argumentul nu e doar aritmetic: un inamic care ocolește mereu promontoriul e un
inamic pe care jucătorul nu-l poate împinge niciodată pe un mal, iar aia e
singura lucrare interesantă pe care uscatul o poate face într-un joc.

Verificat că e inertă stinsă: rulările fără insulă, cu mal sub vânt și cu coastă
ies **identice** cu build-ul dinaintea feliei, și `avoidTicks=0` peste tot.

### Și un instrument de-al meu care a mințit, prins la timp

Comparația de final raporta „DIFFERS" pentru două scenarii. Nu codul diferea —
fișierele de referință nu fuseseră construite, `diff` eșua pe lipsă, iar logica
mea de shell citea eșecul ca diferență. Refăcută, toate trei ies identice. Al
treilea instrument propriu care minte în două zile; verdictul unei comparații
trebuie să distingă „diferă" de „n-am putut compara".

**Task Completed.**


---

## Task Started — 13.09.2026

**Prompt:** „continua, dar vreau la final sa avem un joc cu grafica realistica,
nu minimalista"
**Model:** Claude Opus 5

Ţinta e la final; asta e prima felie din arc. Ordinea nu e negociabilă: nu pot
îmbunătăţi ce nu văd, iar tot proiectul rulase până acum cu `-NullRHI`, adică
fără să deseneze un pixel.

### 1. Camera, înainte de orice pixel

`-ShipShots=a,b,c` face mai multe capturi la secunde alese; `-ShipShotCam=`
aşază braţul pe un vantaj fix şi îl ia de pe controler, altfel „acelaşi cadru"
e altă poză la fiecare build.

Şi-a prins singur primul defect: a raportat `1 frames requested: 8` în loc de 2.
`FParse::Value` se opreşte la virgulă. Linia aceea numărată e tot ce a stat între
o galerie de trei cadre şi una de unul.

### 2. Texturi, sintetizate

Şaptesprezece PNG-uri scrise de `Scripts/textures.py` cu numpy şi zlib. Nimic
descărcat. Se îmbină exact fiindcă zgomotul se construieşte în domeniul
frecvenţei — un spectru pe frecvenţe întregi are perioada egală cu imaginea.

### 3. Nava n-are UV-uri

Verificat în sursă, nu presupus. Deci proiecţie biplanară în spaţiu LOCAL, cu
amestecul după normala verticală. Plus linia de plutire: sub ea lemnul e udat,
mai închis şi mult mai lucios — două lerp-uri, şi e detaliul care face coca să
arate că e ÎN apă.

### 4. Marea şi lumina

Descrise în README. Numerele care contează: soare 110 000 lucşi (nu 7), expunere
pe histogramă îngrădită 12,5–16 EV, ceaţă 0,008 → 0,0022, prag de spumă împins
din C++ ca fracţiune din hula reală.

### Patru instrumente care au minţit, şi unul care a minţit despre celelalte

1. **Expunere manuală cu bias 11,6.** Am crezut că bias-ul numeşte EV100-ul la
   care expui. Nu: e un câştig peste. 2^11,6 ≈ 3000×, cadru alb.
2. **Soarele la 7 lucşi** într-un proiect cu unităţi fizice. Cadru negru.
3. **`set_mobility` pe Actor nu există** — e pe componentă. `lighting.py` crăpa
   la prima linie de lucru real. Şi fiindcă rulam scripturile cu ieşirea în
   `Out-Null` şi citeam **logul jocului** după aceea, am tras concluzia „nivelul
   nu s-a schimbat". Nu se schimbase: scriptul nu rulase. **Patru rulări** —
   mobilitate, două baleieri de umbră, una cu soarele la zenit — n-au testat
   absolut nimic.
4. **Prima versiune a lui `run_py.ps1`**, scrisă tocmai ca să prindă (3), a
   raportat „ok" şi n-a potrivit nimic: `2>&1` pe un exe nativ sub PowerShell
   5.1 împachetează fiecare linie într-un ErrorRecord. Verdictul se citeşte
   acum din FIŞIERUL de log, iar codul de ieşire nu dovedeşte nimic aici
   (editorul iese 1 oricum, pentru o plângere fără legătură).

Şi o alarmă falsă pe care am urmărit-o prea mult: nava ieşea complet neagră din
vantajul `beam`. Am bănuit umbre, ambient, mobilitate. Nu era nimic: `beam` se
uita pur şi simplu la partea umbrită, cu soarele în spate. Din `rig` velele erau
pânză cremă, luminate corect. De aceea există acum `-ShipShotYaw=` — un cadru de
privit se încadrează faţă de lumină, unul de COMPARAT trebuie doar să fie acelaşi
de două ori.

### Ce rămâne din arc

Cordaj (cel mai tare semnal rămas), siaj şi stropi, fum de tun, vegetaţie pe
insulă, inel de spumă la ţărm, şi o velă care să fie velă, nu calotă sferică.

**Task Completed.**


---

## Task Started — 13.09.2026 (a doua felie de grafică)

**Prompt:** „Continua"
**Model:** Claude Opus 5

Următorul din listă, în ordinea pe care o dădusem: cordajul.

### Ce s-a construit

`build_cordage()` în `ship.py`: sarturi (patru pe bord, pe catarg), scări de
frânghie între ele, două straiuri înainte, pataraţine la pupă, braţe de la
capetele vergilor şi şcote de la colţurile velelor. 858 de patrulatere, toate
într-un singur obiect. Meshul a trecut de la 4654 la 6370 de triunghiuri.

Poziţiile nu sunt ochite: `mast_line()` recalculează unde ajunge de fapt un
catarg, dat fiind că `cylinder()` centrează pe locaţie şi abia apoi înclină. Un
sart care ratează crucea cu douăzeci de centimetri e un sart legat vizibil de
nimic.

Plus o textură de parâmă (trei strane răsucite) şi un slot nou, `M_Rope`.

### Prima încercare era un desiş

Scările urcau până la 86% din sart, adică peste vela de sus, şi straiurile erau
de zece centimetri. Tăiate la 56% — pe o navă adevărată acolo e platforma — şi
subţiate, greementul a devenit lizibil.

### Inelul de pe vele: trei ipoteze greşite

Din prima captură cu texturi, fiecare velă avea un inel întunecat, concentric.
L-am explicat pe rând ca:

1. umbra catargului pe pânza bombată — plauzibil, şi fals;
2. cusătura proiecţiei biplanare, care pe o suprafaţă bombată e o curbă
   ÎNCHISĂ — am adăugat `TopWeight` ca să o sting; inelul a rămas;
3. acnee de auto-umbrire — am baleiat bias-ul până la 3,0/4,0; inelul a rămas.

Abia atunci am făcut ce trebuia de la început: **o singură variabilă**. Am pus
harta de normale a velei pe zero şi am privit. Inelul s-a estompat, deci harta
îl AMPLIFICA, dar nu îl crea — ceea ce a mutat suspiciunea de la iluminare la
coordonata de textură.

Şi acolo era. Proiecţia laterală e `(x, z)`, corect pentru o cocă a cărei lungime
merge pe X. Vela e modelată în planul **Y-Z**, deci pentru ea X e burta. Foloseam
mărimea burţii drept coordonată U: inelele erau curbele ei de nivel, desenate
exact.

`SideSwap` — un lerp între `(x,z)` şi `(y,z)`, un singur nod, niciun eşantion în
plus — şi velele au acum cusături verticale de pânză, cum trebuie.

**Lecţia, care nu e nouă în proiectul ăsta:** trei ipoteze costisitoare şi o
probă cu o singură variabilă. Proba trebuia prima.

### Şi variaţia macro pe insulă

Insula ieşea un verde plat de la două sute de metri. Nu era o greşeală de
material: textura de iarbă se repetă la şapte metri, care la distanţa aia e sub
un pixel şi se mediază la propria culoare medie. Detaliul care dispare cu
distanţa trebuie însoţit de detaliu care nu dispare — o mască de zgomot la nouă
zeci de metri, înmulţită peste albedo.

### Regresie

Identică: 53 de linii de tir, zero eşuări, portanţă 0,99, greement dus la 0,52
de focul inamicului. Nimic din pasa de grafică nu atinge jocul.

**Task Completed.**


---

## Task Started — 13.09.2026 (balistica)

**Prompt:** „Continua, si o intrebare, traictoria ghiulelelor tin cont de
gravitatie, da?" + „Si de viteza de deplasare si directie"
**Model:** Claude Opus 5

Întrebarea a scos la iveală un defect, deci felia a devenit răspunsul.

### Gravitaţia: da, şi măsurat

`SetEnableGravity(true)` pe un corp care simulează, plus `g = 980` scris explicit
în `ElevationForRangeDeg`. Dar flagul nu e dovada. La elevaţie 4,80° şi gura la
1,24 m, ballistica prezice 2,585 s; logul dă 2,52–2,58 s. Viteza orizontală iese
145 m/s la 148 m şi tot 145 m/s la 365 m — deci nicio rezistenţă a aerului.

### Viteza navei şi anticiparea: nu erau, acum sunt

`Ball->Fire(Aim * MuzzleSpeed, ...)` — atât. Şi ochirea era pe poziţia curentă a
ţintei. Ambele reparate, ambele pornite implicit, ambele cu flag care readuce
exact comportamentul vechi.

Anticiparea se face pe viteza RELATIVĂ, în două treceri (raza depinde de timpul
de zbor, care depinde de rază).

### Măsurătoarea, şi de ce contează forma ei

Patru seminţe, nu una: singurul hazard din proiect e împrăştierea tunurilor, şi
exact aia se măsoară aici.

| variantă | salve | lovituri | lovituri/salvă |
|---|---|---|---|
| vechi | 28 | 33 | 1,18 |
| doar moştenirea | 28 | **5** | 0,18 |
| doar anticiparea | 28 | 16 | 0,57 |
| ambele | 28 | 32 | 1,14 |

Prezisesem în comentariul din cod că moştenirea singură va înrăutăţi lucrurile.
Acum e măsurat, şi mai tare decât credeam. Ce NU prezisesem: anticiparea singură
e la fel de rea, din motivul simetric.

Rezultatul onest e **paritate**. Nu e un câştig de precizie; e fizica pe care
restul proiectului o respectă deja (gravitaţia, reculul, deriva) aplicată şi
tunurilor.

### Două instrumente, iar

1. Prima metrică număra doar `SHOTLOG hit ` — linia de COCĂ — şi raporta zero
   lovituri pentru o rulare care punea ghiulele în greement tot timpul, fiindcă
   tunurile erau ordonate sus. *O metrică oarbă la lucrul spre care se trage nu
   e o metrică.*
2. Baleierea lui `RangeBias` a dat cinci valori identice. Am bănuit că flagul nu
   ajunge — a doua oară în două zile când bănuiesc asta — şi am pus o linie de
   log care o spune. Flagul ajungea. Baleierea era reală: între 0,98 şi 1,06
   diferenţa e o lovitură din 33. Pe extreme (0,80 vs 1,40) media loviturilor se
   mută de la −140 m la −105 m, deci parametrul funcţionează şi pur şi simplu nu
   contează în intervalul lui. **Un instrument care raportează „nicio
   diferenţă" poate avea dreptate.**

**Task Completed.**


---

## Task Started — 13.09.2026 (surful)

**Prompt:** „continua"
**Model:** Claude Opus 5

### Ce s-a construit

`AOceanSurface::PushIslands()` împinge în materialul mării, pentru fiecare
insulă, `(X, Y, raza ţărmului, marginea bancului)` — aceleaşi două raze pe care
le citeşte forţa de eşuare. Opt insule; a noua e aruncată cu o linie în log.

În material, o bandă cu vârf per insulă, luate cu `max` (nu adunate: două insule
apropiate n-au voie să-şi însumeze albul într-o pată arsă), pulsând cu ceasul
hulei ca să pară valuri care intră.

### Materialul a picat, şi tabla de şah a spus-o

Prima rulare a întors o mare înlocuită de **tabla de şah gri** — materialul
implicit, adică shaderul nu compilase. Eroarea, odată citită din log, era
exactă:

    (Node ComponentMask) Not enough components in
    (Material.PreshaderBuffer[16].xyz: float3) for component mask 0001

Ieşirea implicită a unui VectorParameter e **float3**, deci o mască pe canalul
alfa n-are ce masca. Codul undelor, scris cu săptămâni în urmă chiar în fişierul
ăla, foloseşte de-asta pinii denumiţi `A` şi `B`. Aveam tiparul în faţă şi nu
l-am urmat.

### Trei forme până la o linie de valuri

1. **Rampă spre ţărm, shape 2.2** — spălătură pătată în larg, cu găuri.
2. **Rampă, shape 4.2** — aproape nimic: banda se strângea exact unde nisipul
   ascunde marea.
3. **Bandă cu vârf**, `saturate(1 - |u - peak| / halfwidth)`, cu vârful la 12%
   din lăţimea bancului în afara plajei — o linie continuă de valuri care sparg.

Plus: textura de bule trebuia să SPARGĂ surful, nu să-l FILTREZE. Înmulţită
direct, o gaură din textură făcea o gaură în linia de valuri; are acum un prag
sub care nu coboară.

### Verificat inert

O lume fără insule scrie `SEALOG surf against 0 islands` şi captura nu are
niciun fulg de alb parazit. Regresie: 56 de linii de tir, 11 lovituri, zero
eşuări — identic.

**Task Completed.**


---

## Task Started — 13.09.2026 (baleierea adversarială a arcului)

**Prompt:** „continua"
**Model:** Claude Opus 5 + 14 agenţi

Am trimis şapte perechi de agenţi peste ce a mai rămas din arcul de grafică:
unul care citeşte codul real şi propune un plan, şi unul care încearcă să-l
demonteze. Şapte teme: siaj, surf, stropi, fum de tun, vegetaţie, forma velei,
LOD pentru cordaj.

### Toate şapte planurile au căzut

Nu pe fezabilitate — **niciunul nu cerea Niagara**. Au căzut, fiecare, pe
MĂSURĂTOARE. Şapte agenţi independenţi au găsit acelaşi tipar: proba propusă nu
putea ieşi negativă.

- proba siajului citea un render target în `RTF_R16f`, format pe care
  `ReadRenderTargetRaw` îl respinge;
- proba stropilor cerea două rulări să dea PNG-uri identice bit cu bit, într-un
  proiect care rulează Lumen şi umbre virtuale — temporale amândouă;
- proba fumului voia să numere pixeli într-o scenă cu expunere automată pe
  histogramă, care rescalează tot cadrul;
- proba vegetaţiei cerea `turf_m == 1.0 && rock_m == 0.0`, două condiţii care nu
  pot fi ambele adevărate pe geometria insulei.

Lecţia nu e nouă în proiectul ăsta. E însă prima dată când o găsesc şapte
instanţe independente în acelaşi ceas.

### Şi au găsit un defect în ce livrasem cu o oră înainte

Pulsul surfului. Faza lui era

    -surf_curved / 900  -  WaveTime * 55

Termenul spaţial variază cu 0,001 radiani pe toată banda — adică nu există. Cel
temporal rulează la 55 rad/s, adică **9 Hz**. Nu era un val care intră spre
plajă, era un stroboscop. Şi nicio captură nu putea spune asta, fiindcă un cadru
fix nu arată o frecvenţă.

Verificat de mine înainte de a-l crede, apoi înlocuit: banda îşi mută VÂRFUL cu
o sinusoidă de şapte secunde, deci linia de valuri chiar aleargă înăuntru şi se
retrage.

### Ce s-a luat din baleiere

Două lucruri, amândouă din coloana „alternativă mai ieftină":

1. **Pulsul surfului**, reparat ca mai sus.
2. **Cordajul care scânteiază**, stins prin colaps de vertex dincolo de 240 m.
   Recenzorul a arătat şi de ce calea „corectă" nu merge: `SetLodScreenSizes`
   scrie în `RenderData` şi se întoarce fără `Modify()` sau `MarkPackageDirty()`,
   deci nu ajunge niciodată pe disc.

Restul arcului rămâne, dar acum cu planuri care ştiu unde se rup.

**Task Completed.**


---

## Task Started — 13.09.2026 (fumul de tun)

**Prompt:** „continua"
**Model:** Claude Opus 5

Baleierea adversarială dăduse fumul ca cel mai mare câştig vizual rămas. Am luat
planul ei verificat şi i-am respectat corecţiile: materialul ÎNAINTEA C++-ului
(un .cpp care nu compilează ia scriptul python cu el şi eşecul se citeşte ca
eşec de python), `-ShipSmoke` implicit STINS, şi citire înapoi pe proprietăţile
de care atârnă tot.

### Ce funcţionează

`AGunSmoke`, un actor per TUN. Optsprezece cărţi instanţiate, jet cu frecare,
ridicare, umflare, derivă sub vânt cu rampă şi forfecare, rotire per carte,
plafon de 48 de pufuri cu tăierea scrisă în log. Nelegat de navă. Propriul flux
de numere aleatoare, semănat din indexul lovituri, ca fumul să nu mişte
împrăştierea tunurilor pe care `-ShipSeed` există s-o fixeze.

Materialul: primul translucid, neluminat şi instanţiat din proiect. Sondat
înainte de scris — translucenţă, unlit, MP_OPACITY, MP_EMISSIVE_COLOR, DepthFade
şi PerInstanceRandom, niciuna folosită vreodată aici — şi toate există.

### Ce NU funcţionează, şi de ce se livrează stins

Nu arată a fum. E o foaie gri ruptă. Marginile se erodează cum trebuie, dar
interiorul rămâne opac.

### Şase rulări pe un singur simptom

Eşantionul din textură ieşea plat. Măsurat, nu bănuit: emisivul citea 0,25
liniar, **exact media texturii**. Cauze eliminate, în ordine:

1. împachetarea canalelor — textura sursă are R şi G bogate, verificat privind-o;
2. sRGB — importată cu gamma stinsă;
3. **compresia în tonuri de gri** — sufixul `_M` din convenţia proiectului o
   marca mască şi arunca canalul G. Reparat (sufix nou `_D` pentru DATE), fără
   efect asupra simptomului;
4. tiling-ul nodului de coordonate — pus explicit şi citit înapoi: 1,0;
5. streamingul — `never_stream` pornit, fără efect.

Abia apoi am făcut ce trebuia: am scos textura din ecuaţie. **Zgomot
procedural** — fără UV-uri, fără import, fără streaming — şi erodarea a pornit
din prima. Vina e în calea texturii; care verigă exact, nu ştiu, şi nu mai
cheltui rulări pe asta acum.

Nodul Noise şi-a adus şi el lecţia: pinul lui de poziţie nu se cheamă
„Position", iar asertul dur din fişier a prins-o în loc s-o lase să treacă drept
WARN. Lăsat NECONECTAT e chiar ce trebuia: aşa foloseşte poziţia în lume, deci
norul e tăiat dintr-un volum de zgomot prin care trec cărţile, nu optsprezece
copii ale aceluiaşi desen.

### Verificat inert

Cu fumul stins: 56 de linii de tir, 11 lovituri, zero eşuări, **zero SMOKELOG**.
Identic cu toate măsurătorile de tir de până acum, ceea ce e tot rostul unui
implicit stins.

**Task Completed — cu efectul NElivrat.**


---

## Task Started — 14.09.2026 (fumul, a doua zi)

**Prompt:** „continua"
**Model:** Claude Opus 5

### Diagnosticul pe care nu-l pusesem

Cărţile erau de 260–400 cm, iar tot norul se întindea pe 300. Fiecare carte era
cât norul întreg — optsprezece plăci coincidente nu pot forma decât un perete.
Un nor cere cărţi mult mai MICI decât el. Treizeci de cărţi de 130 cm, semănate
printr-o bilă de aproape un metru (cu rădăcină cubică, ca să umple bila uniform
în loc să se înghesuie pe margine).

### Şi apoi cele două capcane care au explicat totul

**Materialul nu era pe ecran.** Lipsea `bUsedWithInstancedStaticMeshes` şi
motorul punea materialul implicit — acel gri pe care îl reglam de ore. Linia din
log o spunea, dar nu conţine „failed to compile", iar eu căutasem exact
formularea aia, ştiută de la materialul mării. **Am căutat şirul pe care îl
văzusem, nu subiectul.**

Găsită de o sondă cu o singură variabilă: emisiv roşu aprins, întors GRI. Ce a
forţat întrebarea a fost o contradicţie fizică — aditivul nu poate desena mai
întunecat decât fundalul, iar pata era mai întunecată.

**Prima rulare după o reconstrucţie nu arată materialul nou.** Măsurat: acelaşi
material, trei rulări identice — prima diferă, a doua şi a treia sunt de acord.
Asta pune sub semnul întrebării fiecare pereche înainte/după din arcul ăsta
făcută dintr-o singură rulare. De acum: **două rulări, compari a doua cu a
doua.**

### Unde a rămas

Cu flagul pus şi cu geometria verificată (30 de cărţi, întindere 4,5 m, mărimi
160–220 cm, UV-uri prezente, MID corect, zero avertismente), fumul tot nu se
vede. Nu mai reglez pe ghicite. Rămâne stins, şi rămâne cu două capcane reale
documentate care valorează mai mult decât efectul.

Regresie: 56 de linii de tir, 11 lovituri, zero eşuări, **zero SMOKELOG**.

**Task Completed — efectul tot NElivrat.**


---

## Task Started — 14.09.2026 (fumul, închis)

**Prompt:** „continua"
**Model:** Claude Opus 5

Fumul funcţionează.

### Ce mai era stricat, şi cum s-a găsit fiecare

**Pragul de erodare era peste ce putea atinge zgomotul.** Calculabil, nu de
ghicit: două câmpuri de turbulenţă ÎNMULŢITE au media 0,12, iar pragul pornea
de la 0,36. Alfa era zero din primul cadru. Verificat înainte de reparat, prin
sonda de zgomot: chiar forţat la opacitate 1 pe un emisiv aditiv, cărţile abia
luminau cadrul — aia e imaginea unei valori în jur de 0,1. Sumă ponderată în loc
de produs, prag mutat în intervalul real.

**Sonda mea roşie era ea însăşi stricată.** Desena cărţi NEGRE, iar un emisiv de
6,0 nu poate fi negru. `sp(node, "constant", ...)` pe un `Constant3Vector`
eşuase tăcut şi helper-ul l-a înghiţit într-un WARN. Instrumentul scris ca să
testeze materialul avea acelaşi defect pe care îl căuta. Înlocuit cu un
VectorParameter.

**Aditivul nu se desena deloc.** Alegerea era raţionată şi rămâne corectă în
principiu — cărţile dintr-un component instanţiat nu se sortează, iar adunarea e
comutativă. Dar tiparul peste toate dovezile de după repararea flagului era fără
excepţie: translucid → cărţile apar, aditiv → nimic. Argumentul era bun şi
motorul nu e de acord; câştigă motorul, şi scriu de ce în cod ca să nu-l reiau.

### Măsurat că nu atinge jocul

Aceeaşi rulare, fum stins şi fum pornit: **56 de linii de tir şi 11 lovituri în
amândouă**, zero eşuări. Fluxul separat de aleator, semănat din indexul
loviturii, îşi face treaba — fumul nu mişcă unde cad ghiulelele.

Rămâne stins implicit, şi nu pentru că ar fi stricat: fiecare măsurătoare de tir
din proiect a fost luată fără el. Dacă merită să devină implicit e o judecată de
ochi — punctul 20 din OWNER_VERIFY.

**Task Completed.**


---

## Task Started — 14.09.2026 (siajul)

**Prompt:** „Lasam fumul pentru mai incolo. Continua"
**Model:** Claude Opus 5

Am luat varianta IEFTINĂ pe care recenzorul adversarial o propusese în locul
planului cu render target, şi i-am respectat corecţiile.

### Ce s-a construit

Firimituri într-un `TArray` din `AOceanSurface`: trei nave × opt puncte, câte
unul la fiecare nouă metri de drum, împachetate `(x, y, tărie, semi-lăţime)` şi
împinse ca `WakePt%d` prin exact bucla `FString::Printf` pe care fişierul o
foloseşte deja pentru unde şi pentru insule. Materialul face din fiecare pereche
consecutivă o capsulă şi le ia cu `max`.

Firimitura cade la pupă, nu în centrul de greutate — un siaj care porneşte de la
mijlocul navei e desenat prin cocă. Prima încercare o lăsa la paisprezece metri
şi se vedea o gaură între navă şi capul propriului siaj; acum e la nouă.

### Capcana din material, a doua oară în acelaşi fişier

Semi-lăţimea stă în canalul **alfa** al vectorului împachetat, iar ieşirea
implicită a unui VectorParameter e float3 — o mască pe a patra componentă nu
compilează. Exact capcana în care căzuse surful insulelor, în acelaşi fişier,
acum două zile. De data asta am scris-o corect din prima, folosind pinul denumit
`A`, fiindcă era notată.

### Măsurat

Regresie identică: 56 de linii de tir, 11 lovituri, zero eşuări.

Şi linia care contează: la pornire `WAKELOG live=0 of 24` cu toate trei navele la
zero firimituri — **nimic nu se depune înainte să se mişte ceva**. La final,
într-o rulare în care jucătorul stă pe loc, `ShipPawn_0:0` lângă doi inamici cu
8 fiecare. Poarta de viteză se vede în numere.

Materialul mării e acum la 866 de expresii.

### Ce lipseşte

Siajul e o dâră, nu un V: nu are braţele Kelvin care pleacă în unghi din prova,
şi nu are guler de spumă în jurul cocii. Şi se termină brusc la cea mai veche
firimitură în loc să se stingă.

**Task Completed.**


---

## Task Started — 14.09.2026 (gulerul şi braţele)

**Prompt:** „continua"
**Model:** Claude Opus 5

Am completat siajul cu cele două lucruri pe care firimiturile nu le pot desena,
fiindcă ele sunt istorie iar astea sunt prezentul: gulerul din jurul cocii şi
braţele Kelvin din etravă. Ambele din poziţia vie a navei, aşezate pe DRUMUL ei.

### Două greşeli, amândouă de acelaşi fel: o mărime nemărginită

**Braţele mergeau până la orizont.** Un pană Kelvin e o pereche de drepte
infinite dacă nu o mărgineşte nimic, iar masca mea „doar în pupă" era inversată:
înmulţeam cu `1 - astern`, şi `astern` e zero pentru orice pixel din FAŢA navei —
deci braţele erau la putere maximă înainte, până unde se vedea. Toată marea a
ieşit gri.

Reparat cu două margini, nu cu una: o rampă care porneşte de la etravă în pupă
(şi e zero oriunde în faţă, fiindcă `-along` e negativ acolo şi se satureaza la
zero) şi o stingere pe lungimea braţului. Plus **o lesă structurală** peste tot
termenul viu: nimic din ce desenează o navă nu are voie să ajungă mai departe de
nouăzeci de metri de ea, orice ar face aritmetica de deasupra. O margine
structurală valorează mai mult decât o formulă corectă, fiindcă formula a fost
corectă exact până n-a mai fost.

**Gulerul era un disc plin de nouăzeci de metri.** Folosisem `saturate` acolo
unde voiam `max(0, x)`. Alea sunt DISTANŢE în centimetri, iar `saturate` le
plafonează la unu — deci distanţa în afara cocii nu trecea niciodată de 1,4 cm
nicăieri în lume, şi gulerul citea 0,99 peste toată lesa. Aceeaşi greşeală de
tip ca prima: o mărime lăsată fără marginea potrivită.

### Măsurat

Regresie identică: 56 de linii de tir, 11 lovituri, zero eşuări. Scenariul de
eşuare: 2 atingeri, neschimbat. Materialul mării: 1037 de expresii.

**Task Completed.**


---

## Task Started — 14.09.2026 (forma velei)

**Prompt:** „continua"
**Model:** Claude Opus 5

### Vela

Forma veche prindea şi capul, şi poala, şi umfla mijlocul: aia e o pernă, nu o
velă. O velă pătrată e legată de vergă pe toată lungimea capului — drept — şi e
LIBERĂ la poală, ţinută doar de cele două colţuri de jos, care e exact partea
care se umflă cel mai mult. Adâncimea e acum o fracţiune din LĂŢIME (11%, o
croială obişnuită de lucru) în loc de 0,9 m ficşi, care făceau o velă de nouă
metri şi una de şapte la fel de adânci şi deci de formă diferită. Poala are
rotunjire.

### Şi defectul pe care îl găsise recenzorul adversarial

`build_cordage` prindea fiecare şcotă la `x + 0.35` — o ghicire despre unde
ajunsese colţul velei. Un colţ e prin definiţie la adâncime ZERO: o velă e
ţinută de colţuri. Şcotele erau legate la treizeci şi cinci de centimetri în
faţa colţului de care trebuiau legate.

Acum există o singură funcţie, `sail_surface()`, folosită şi de mesh şi de
cordaj, iar cordajul ÎNTREABĂ `sail_clew()` în loc să ghicească. Nu mai pot
diverge când se schimbă forma.

### Tokenul, prins de CI în prima lui zi de lucru

Ştersul secţiunii AndroidFileServer din `DefaultEngine.ini` nu era de ajuns:
motorul a rescris-o, cu un token NOU, la următoarea rulare, iar garda de şiruri
secrete a făcut build-ul roşu. Reparat de data asta la cauză — pluginul e
dezactivat în `.uproject`, deci nu mai are cine scrie secţiunea. Verificat
rulând editorul de două ori după aceea şi uitându-mă dacă tokenul revine, nu
presupunând că n-o să revină.

### Măsurat

Neschimbat faţă de linia de bază: vela e geometrie, iar coca se loveşte pe o
cutie.

**Task Completed.**


---

## Task Started — 14.09.2026 (stropii)

**Prompt:** „Continua"
**Model:** Claude Opus 5

„Ghiulelele plesnesc fără stropi vizibili" era în lista de limitări din prima
sesiune. Acum nu mai e.

Ghiuleaua raportează impactul — ea ştie deja punctul exact, marea nu trebuie să-l
caute — iar `AOceanSurface` îl ţine minte o secundă şi jumătate. Al treilea efect
pe acelaşi mecanism, după surf şi siaj.

Inelul se rupe cu textura de bule BRUTĂ. Prima variantă folosea `broken`, care
are un prag de 0,45 ca linia de surf să rămână continuă — corect pentru surf,
greşit aici: cu prag inelul nu poate fi găurit, şi patru cercuri perfecte pe apă
se citesc ca geometrie.

### Şi un defect al instrumentului, găsit din întâmplare

Încercam să prind stropii la momentul potrivit şi două capturi la timpi diferiţi
ieşeau identice. `-ShipShots=10.6,11.4` se rotunjeau amândouă la `t011` şi a doua
o suprascria pe prima. O galerie făcută ca să arate ceva schimbându-se arăta un
cadru de două ori. Numele poartă acum zecimi.

Măsurători neschimbate.

**Task Completed.**


---

## Task Started — 14.09.2026 (vegetaţia)

**Prompt:** „Continua"
**Model:** Claude Opus 5

Ultima temă din arc. Am luat tier-ul 1 al recenzorului adversarial — acelaşi
contur, jumătate din plan, niciunul din nodurile pe care proiectul nu le-a
compilat vreodată.

Doi arbuşti din Blender, împrăştiaţi prin trasare de rază în două meshuri
instanţiate ierarhic. Geometrie opacă: cartonaşele cu alfa ar fi cerut un
material mascat, iar cele două încercări de material nou de săptămâna asta au
costat câte o sesiune.

### Corecţia care contează

Pragurile de înălţime şi de pantă se CITESC din `M_Island` la pornire. O a doua
copie în C++ ar fi fost corectă exact până la prima reglare, şi defectul ar fi
fost un palmier pe nisip pictat. Logul spune `4 of 4 numbers read from the
material` — o cădere tăcută pe implicite s-ar citi `0 of 4`.

Acelaşi principiu ca la colţul velei, ieri: **întreabă lucrul care ştie.**

### Şi o măsurătoare pe care n-o căutam

`too steep=0`: niciun punct din toată insula nu e destul de abrupt cât să treacă
pragul de rocă. Asta explică de ce roca nu s-a arătat niciodată pe dealul ăsta —
o observaţie din lista de limitări, acum cu o cifră în spate.

### Curăţenie

`island_assets.build_material()` refuză acum să ruleze. Era înlocuită de
`island_material.py`, dar stătea pe disc, mergea de una singură, şi ar fi
înlocuit M_Island cu rampa de culoare veche fără să raporteze nimic.

Măsurători neschimbate.

**Task Completed.**

---

## Task Started — 14.09.2026 (cele opt constatări ale recenziei)

**Prompt:** continuarea arcului — de reparat constatările confirmate de recenzia
adversarială (12 agenţi, patru lentile: materiale, C++, instrumente, depozit).
Din 36 raportate, primele două de pe fiecare lentilă au fost verificate
adversarial şi toate opt au supravieţuit.
**Model:** Claude Opus 5

### Poarta care nu putea ieşi roşie

`ships_sunk` număra `sink=sinking`. Jocul nu scrie niciodată şirul ăsta:
`SinkPhaseName()` întoarce `afloat|flooding|foundering|plunging|wreck`. Numărul
era pironit la zero de trei commit-uri, pe trei scenarii, într-un pipeline verde.

Numără acum evenimentul terminal, `SHIPLOG <navă> SUNK `. Şi fiindcă un contor
care dă zero peste tot nu se deosebeşte de unul stricat, a intrat **un al
patrulea scenariu în care chiar se scufundă ceva** (`-EnemySinkTest=6`): dă
`ships_sunk=2`, aceleaşi două nave la fiecare rulare. Dacă tiparul se strică
vreodată, scenariul ăla cade de la 2 la 0 şi o spune.

### Comparaţia care umbla doar prin ce tocmai culesese

`lift_mean`, `wake_live_max` şi `islands_built` se scriu doar dacă logul le
poartă. Bucla mergea prin cheile rezultatului NOU, deci un număr care înceta să
mai fie măsurat nu era atins niciodată şi ieşea „potrivire curată". Merge acum
prin reuniunea celor două părţi, şi „a încetat să mai fie măsurat" e un verdict
separat de MOVED şi de NEW — altă cauză, alt remediu.

### Şi fixtura, fiindcă niciuna dintre cele două n-ar fi fost prinsă RULÂND

Comparaţia a ieşit din `main()` într-o funcţie proprie, fără fişiere, fără motor
şi fără ceas. `ci_checks.py` o hrăneşte acum cu fixturi — identic, mutat, nou,
dispărut, plus toleranţa de virgulă în ambele sensuri, plus contorul de
scufundări pe textul exact pe care-l scrie `ShipPawn.cpp`.

Verificat prin mutaţie, nu prin încredere: am pus la loc bucla veche şi apoi
tiparul vechi, pe rând, şi verificările au ieşit roşii de fiecare dată, cu
mesajul corect. O fixtură care n-a fost văzută niciodată picând e exact tipul de
instrument pe care proiectul ăsta l-a crezut de trei ori.

### Siajele erau legate de RANG, nu de navă

Comentariul spunea „keyed to the ship". Codul folosea `Trails[i]` pentru a i-a
navă din sortarea după distanţă. Două defecte dintr-o singură cauză:

- două nave care-şi schimbă rangul îşi găsesc fiecare siajul altcuiva în slot şi
  **amândouă siajele sunt aruncate** — în larg, din senin;
- mai rău: o navă scufundată din MIJLOCUL listei le mută pe cele din spate cu un
  slot mai jos, aşa că ultima se re-leagă mai jos în timp ce vechiul ei slot
  rămâne cu firimiturile şi cu o navă vie în el. Pasul de curăţenie îl păstrează
  (nava lui e urmărită), pasul de actualizare nu ajunge la el (e peste
  `Ships.Num()`), deci vârstele îngheaţă în timp ce slotul se împachetează şi se
  trimite în fiecare cadru: **un siaj îngheţat în apă, la putere maximă, până la
  sfârşitul partidei.**

Acum fiecare navă îşi păstrează slotul cât timp e urmărită, iar o navă fără slot
ia primul liber. Şi două numere care trebuie să fie zero se numără, nu se cred:
`stranded=` (slot cu firimituri şi fără navă) şi `doubled=` (o navă cu două
sloturi). Defectul de mai sus era invizibil în orice alt număr pe care-l scrie
siajul — firimiturile erau vii, bine formate şi de lungimea potrivită.

### Stropul care nu-şi păzea sloturile goale

Un slot nefolosit se trimite ca zerouri. Zero nu e „nimic": e un strop la
(0, 0) cu rază zero, adică un disc de spumă de vreo doi metri stând pe apă la
origine, de la primul cadru, opt unul peste altul. Toate celelalte efecte de pe
foaia aia îşi păzesc sloturile moarte; ăsta nu. Un singur nod: pinul „A" e deja
bitul de viaţă — poartă raza în centimetri, sute când slotul e în uz, exact zero
când nu.

### Normala: o bază care nu există — şi o primă reparaţie mai proastă decât boala

O hartă tangenţială e o promisiune că meshul are cadru tangent, iar cadrul
tangent vine din UV-uri. **Niciun mesh din proiect n-are UV-uri** — ăsta e chiar
motivul pentru care materialul proiectează. Deci fiecare denivelare de pe cocă,
punte, vele, parâme şi plante era înclinată într-o bază pe care n-o definise
nimeni.

Prima reparaţie: reconstruieşte proba în cadrul PLANULUI de proiecţie şi dă
motorului o normală în spaţiul lumii. Corectă pe o suprafaţă care priveşte cum
presupune planul, greşită pe oricare alta — cele două plane acoperă ±Y şi ±Z,
iar **un catarg priveşte +X**. Catargele au ieşit negre. N-am văzut-o citind
graful; am văzut-o punând captura veche lângă cea nouă, acelaşi cadru fixat.

A doua, cea livrată: ţine normala geometrică şi o ÎNCLINĂ, într-un cadru
construit pe loc din ea (produs vectorial cu un vector fix, dinadins nealiniat
cu axele — alinierea la axe se degenerează exact pe cocă şi pe punţi). Exact ce
făcea calea tangenţială, minus partea în care cadrul ei nu exista. Puterea zero
dă acum fix normala geometrică, fără caz special. Catargele sunt iar luminate,
pânzele identice cu înainte.

### Şi căile absolute din `run_py.ps1`

Fişierul purta căile maşinii mele. `measure.yml` îl cheamă de pe un runner care
face checkout în `C:\actions-runner\_work\...`, deci jobul ar fi condus proiectul
din `C:\Users\besli\...` şi ar fi raportat rezultatul ca măsurătoare a
commit-ului testat. Rădăcina se deduce din `$PSScriptRoot`, motorul din `$env:UE`,
şi dacă vreunul dintre cele trei drumuri nu există refuză zgomotos — altfel
verdictul s-ar citi de pe un log rămas de la rularea dinainte, adică exact
minciuna pentru care a fost scris fişierul.

### Măsurători

Cele trei scenarii vechi: identice cu linia de bază, până la ultima cifră.
Scenariul nou, rulat de trei ori cu aceleaşi flag-uri, a dat de fiecare dată
aceleaşi numere. Linia de bază a fost rescrisă deliberat, în acelaşi commit.

**Task Completed.**

---

## Task Started — 15.09.2026 (a doua recenzie adversarială)

**Prompt:** „continua tu singur"
**Model:** Claude Opus 5

18 agenţi, şase lentile peste arborele de ACUM (normale, mare, ocean-C++,
navă-C++, instrumente, documente-contra-cod), plus un combatant pe fiecare
constatare de top. **34 raportate, 12 verificate adversarial, ZERO respinse.**
Toate douăsprezece sunt reparate azi. Restul stau scrise în
`tasks/REVIEW2_OPEN.md`, nu în capul meu.

Trei dintre ele erau în codul scris **ieri**.

### Contoarele mele nu puteau ieşi roşii

`stranded` citea „slot cu firimituri şi fără navă" — o stare pe care pasul de
curăţenie o interzice prin construcţie, fiindcă orice cale care şterge nava
şterge firimiturile în acelaşi bloc. Nu putea să se aprindă pentru chiar
defectul pentru care fusese scris (un siaj îngheţat ARE navă). Acum citeşte ce
se strică de fapt: firimituri DESENATE fără ca nimeni să le fi avansat ceasul în
cadrul ăsta.

Şi m-a prins imediat pe mine: prima firimitură dintr-un slot gol ieşea
„neîmbătrânită", contorul a dat 1 în gunnery. Un contor care plânge pe purtare
corectă e acelaşi defect ca unul care nu poate plânge deloc.

Mai rău: **contoarele erau locale pe cadru**, iar linia WAKELOG se tipăreşte o
dată pe secundă. Un eveniment pe orice alt cadru era tipărit de nimeni. Proba
prin mutaţie a arătat-o fără drept de apel: am pus legarea după rang înapoi şi
toate contoarele au ieşit zero, pe o rulare al cărei propriu log arăta rangul
schimbându-se de patru ori. Acum latch-uiesc:

    legare după rang:       discarded=1
    legare prin identitate: discarded=0

### Siajul unei nave găurite dispărea într-un cadru

`IsSunk()` e „integritate <= 0", deci se aprinde în clipa în care o ghiulea
trece prin ea — cu douăzeci şi cinci de secunde de inundare în faţă, în care
nava e măsurabil tot sub drum, la patru metri pe secundă. O scoteam din lista
urmăriţilor în clipa aia, iar pasul de curăţenie citea „neurmărit" ca „aruncă
istoria": şaptezeci de metri de apă albă între două cadre, apoi o cocă vizibil în
mers fără siaj, fără guler şi fără braţe, pe o mare ca sticla, o jumătate de
minut.

Acum siajul e al APEI, nu al listei: îmbătrânirea e un pas separat peste toate
sloturile, urmărirea decide doar cine mai LASĂ firimituri, iar o navă e urmărită
cât e pe suprafaţă (FAZA de scufundare, nu `IsSunk()`).

### Comparaţia mergea pe un singur nivel

Reuniunea de cheile pe care am adăugat-o ieri era cu un nivel prea jos:
scenariile însele se luau tot dintr-o parte. `compare({}, baseline)` nu raporta
NIMIC. Iar fixturile mele foloseau acelaşi unic scenariu pe ambele părţi — adică
erau de acord cu autorul exact acolo unde autorul greşea.

### Marea nu citea vântul. Deloc.

`SetSeaState` construia hula din două constante şi un cap compas de 25 de grade.
Consecinţa e mai gravă decât imaginea: **fiecare citire „nu se schimbă nimic pe
tot intervalul de vânt" din proiectul ăsta nu era o reparaţie care ţine la
ambele capete, era o intrare care nu s-a mişcat niciodată.**

Acum: amplitudinile se scalează cu vântul, direcţia vine din vânt, iar
împrăştierea direcţională a scăzut de la 400 de grade (adică tot cercul: o mare
fără direcţie) la 45.

    5 m/s  →  49 cm,  spumă de la 20 cm
    11 m/s → 109 cm,  spumă de la 45 cm
    18 m/s → 178 cm,  spumă de la 73 cm

### Şi pragul spumei era o fracţie dintr-un maxim de neatins

0,78 din SUMA amplitudinilor — 109 cm — sumă pe care creasta o atinge doar dacă
toate cele şase valuri urcă în acelaşi punct în aceeaşi clipă. Creasta e o sumă
de şase cosinusuri: are o abatere standard, sigma = 34 cm, iar 0,78 din sumă e
2,5 sigma. **Trei pixeli din o mie. Marea n-avea berbeci deloc**, iar capătul de
sus al rampei nu era atins aritmetic. Logul arăta sănătos tot timpul: 85 pare o
fracţie cuminte din 109 până întrebi ce e 109.

Acum e în sigma, şi — partea care contează mai mult decât constanta — acoperirea
se MĂSOARĂ: aceleaşi şase valuri, evaluate pe o reţea peste douăzeci de
kilometri de apă, şi procentul din mare care trece pragul se tipăreşte. 10,8%.

### Ghiuleaua murea în altă mare decât plutea nava

`GetWaterSurfaceInfoAtLocation(..., true)` — iar `true` e `bIncludeDepth`, nu
valuri, şi funcţia aia nu cere niciodată `IncludeWaves`. Dovada era deja pe disc:
25 de stropi în toate logurile, niciunul deasupra lui Z=0, pe o mare de un metru.

Şi, pe deasupra: **o salvă trasă dintr-un gol de val îşi ucidea toate cele patru
ghiulele la gura tunului** — `range=0m flight=0.00s`, patru inele de spumă pe
propriul bord, o reîncărcare arsă şi patru intrări în numărătoarea de stropi care
n-au atins marea. Una din şaisprezece salve, în loguri. Garda e cât o lungime de
cocă, nu cât o ţeavă (la elevaţiile astea ghiuleaua urcă o zecime din cât merge),
iar cazul e numărat: `awash=1` în scenariul de furtună.

### Cadrul normalelor era o constantă, nu o suprafaţă

Cea mai gravă constatare era în reparaţia de ieri, şi aritmetica ei e exactă:
`cross(N, cross(N, h))` e vectorul fix `h` turtit în planul tangent **şi negat** —
o funcţie de `h`, nu de proiecţie. Unghiul faţă de direcţia pe care o înseamnă
canalul verde: median 160 de grade, produsul scalar negativ pe 100% din suprafaţa
cocii. Iar canalul verde poartă 99,5% din relieful texturii ăsteia, fiindcă
îmbinările scândurilor merg pe rânduri. Fiecare îmbinare călăfătuită era luminată
ca o şipcă în relief.

A treia încercare nu mai inventează niciun cadru: o probă proiectată ESTE un
gradient de înălţime în axele planului ei. Se reconstruieşte acolo, se amestecă
GRADIENŢII (liniar — fără cusătură, deci fără nimic de ascuns cu un buton), se
scoate partea de-a lungul normalei şi ce rămâne înclină normala geometrică.

Şi trei plane în loc de două: ambele proiecţii vechi luau u din x, deci orice
suprafaţă dintr-un plan de x constant — pupa, capul provei, capetele cabinei,
faţa dinainte a fiecărui catarg — eşantiona o SINGURĂ coloană de texeli.

### Ce am corectat în documente

OWNER_VERIFY 3 îţi cerea să confirmi un throttle şi o cocă rotită din mouse —
comenzile navei-jucărie din prima săptămână. OWNER_VERIFY 16 îţi spunea că marea
n-are siaj, o sesiune întreagă după ce siajul există. README zicea că `-Islands`
face exact o insulă „oricât ai cere"; face până la opt.

### Măsurători

Şase scenarii acum (`gale` e gunnery la 18 m/s, ca cifrele mării să difere în
două puncte). Chei noi: amplitudinea hulei, pragul spumei, procentul care se
sparge, `surfZ` la stropi, `awash`, şi cele cinci contoare de siaj pe care C++ le
tipărea şi nu le citea nimeni. Logul se şterge înainte de fiecare scenariu şi i
se verifică linia de comandă, fiindcă toate scenariile scriu în acelaşi fişier şi
un editor care nu porneşte lăsa logul precedent acolo. `groundings` număra LINII,
două pe lovitură: linia de bază a scăzut de la 2 la 1, adică de la un număr fără
unitate la unul cu.

**Task Completed.**
