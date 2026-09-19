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

---

## Task Started — 15.09.2026 (cele 22 neverificate)

**Prompt:** „Continua"
**Model:** Claude Opus 5

Restul recenziei a doua: cele 22 de constatări raportate şi neverificate. Nouă
mai erau în picioare după cele patru commit-uri de dimineaţă, şi au plecat la
verificare câte un agent fiecare, împotriva arborelui de ACUM. **Opt încă
ţineau, una nu ţinuse niciodată.** Restul de treisprezece erau despre documente
şi despre lucruri deja reparate.

### Controlul negativ putea trece din motivul greşit

Ăsta e cel mai grav, fiindcă e lucrul care certifică toate celelalte. Pasul care
strică dinadins o textură şi cere verificărilor s-o prindă nu verifica niciodată
că STRICĂCIUNEA A APUCAT SĂ SE APLICE. `sed -i` iese cu 0 când nu potriveşte
nimic şi rescrie fişierul octet cu octet. Redenumeşte `STRIPS` şi: stricăciunea
nu mai prinde, verificările trec pe un arbore INTACT, iar pasul anunţă „garda e
oarbă" despre o gardă perfect sănătoasă. Controlul ar fi fost lucrul stricat, şi
ar fi dat vina pe gardă.

Acum compară cu backupul înainte de orice verdict, cere ca refuzul să numească
tiparul pe care l-am stricat NOI (şase locuri diferite tipăresc „does not
divide"), şi verifică restaurarea. Probat local în ambele sensuri: stricăciunea
reală e prinsă din motivul corect, iar redenumirea constantei face controlul să
refuze cu un mesaj care numeşte constanta, în loc să mintă.

### Plantele şi vopseaua se despărţeau pe orice insulă de altă mărime

`Island.cpp` scala linia ţărmului cu mărimea insulei; `M_Island` citeşte Z
absolut din lume. Măsurat înainte: la scara 1,27 plantele începeau la 573 cm iar
vopseaua trecea la iarbă la 450 — 123 cm de iarbă pictată pe care nu creştea
nimic; la 0,55 stăteau pe 205 cm de NISIP pictat, adică exact defectul pe care
citirea numerelor din material trebuia să-l facă imposibil.

Două caractere (`* Scale`) au plecat de pe singura margine care pretinde că e
numărul materialului. Logul tipăreşte acum ambele numere, diferenţa lor e o
cheie în linia de bază, şi a intrat un scenariu `lee_shore` cu insule la 1,27 —
singurul din suită în care cele două numere POT să nu fie egale.

### Anticipaţia scădea o viteză pe care ghiuleaua n-o primea

Două locuri derivau „ce duce ghiuleaua cu ea" din acelaşi flag, separat, şi doar
unul îl citea. Acum e un singur vector. Măsurat, aceeaşi navă, aceeaşi viteză, un
singur flag diferenţă:

    inherit=1  velFwd=6.58  lead=6.1 m
    inherit=0  velFwd=6.58  lead=3.1 m

Şi de-aia perechea care păzeşte asta are `-ShipRudderTest`: prima versiune trăgea
din loc, unde viteza proprie e zero, ambele ramuri sunt de acord banal şi
măsurătoarea e nulă. A citit 4,0 m de ambele părţi şi n-a dovedit nimic.

### O lovitură fără ţintă era notată ca lovitură în plin

`along=+0.0 lateral=+0.0` e ce tipăreşte o lovitură perfectă. Era şi ce tipărea o
ghiulea trasă în gol, sau una a cărei ţintă se scufundase în zbor — un sfert din
sfârşiturile din logul scenariului de furtună. Acum scrie `nomark`.

### Şi patru mai mici, toate din aceeaşi familie

`PushIslands` îşi punea zăvorul chiar şi când nu găsea nimic, deci o suprafaţă
care a tictăcit o dată înainte să existe insule renunţa definitiv — în timp ce
antetul descria exact cazul ăla ca motiv al reîncercării. WAKELOG tipărea `of 24`
şi `splashes=8`, constante de compilare în formă de măsurători (o singură valoare
distinctă în 198 de linii); le-au luat locul `slots` şi `shortest`, care se mişcă.
Trei butoane din categoria Wake nu erau citite de nimeni, deşi fraţii lor de pe
liniile de deasupra erau — jumătate din guler era al actorului şi jumătate al
materialului. Iar `ScatterRangeCm`, rampa de culoare a apei, era tot 150 cm fixe
pe acelaşi semnal de creastă de pe care mutasem pragul spumei dimineaţă: 4 sigma
la vântul pentru care fusese scrisă hula, 8,9 într-un calm şi 2,4 într-o furtună
din clipa în care hula a început să urmeze vântul.

### Şi numerele din documente

Şapte constatări erau despre cifre pe care documentele le publicau şi codul nu le
mai avea. Măsurate, nu amintite: nava are **7074** de triunghiuri, nu 4654; sunt
**opt** instanţe de material, nu şase; `textures.py` scrie **21** de PNG-uri, nu
17; fumul are **30** de cărţi, nu 18. Plus trei puncte din OWNER_VERIFY pe care
le-am găsit singur trecând prin listă: îţi cereau să confirmi un throttle care nu
există, să te uiţi la şase materiale din opt, şi să iei o decizie despre Visual
Studio luată acum o lună.

### Măsurători

Nouă scenarii. Chei noi: `scatter_range_cm` şi `scatter_span` (trebuie să se
mişte în direcţii OPUSE — prima urmează vântul, a doua e ce ţine cuplarea pe
loc), `turf_band_gap_cm`, `islands_pushed`, `lead_max_m` cu `fire_velfwd_max` ca
gardă, `wake_slots_max`, `wake_shortest_max`. Linia de bază rescrisă deliberat,
apoi toată suita rulată din nou peste ea: **toate măsurătorile se potrivesc**,
inclusiv cheile şi scenariile noi.

**Task Completed.**

---

## Task Started — 15.09.2026 (vântul, în ce se îndoaie)

**Prompt:** „Continua"
**Model:** Claude Opus 5

Primul gol din lista onestă de la OWNER_VERIFY 18: **nimic nu se mişca în vânt**.
Marea răspundea la vânt de dimineaţă; plantele şi greementul nu, deloc.

Materialul navei mută acum vârfurile cu vântul, iar C++-ul împinge direcţia,
viteza şi un ceas în materiale dinamice — insula pentru plantele ei, fiecare navă
pentru cele şapte sloturi ale ei. Patru scalari, toţi cu implicit „calm absolut",
deci o piesă al cărei proprietar nu împinge nimic e exact la fel de nemişcată ca
înainte.

### Forma, nu amplitudinea

Îndoită sub vânt ÎNTÂI, şi oscilează în jurul îndoirii; îndoirea creşte cu
viteza; o rafală lentă o umflă şi o lasă; iar deplasarea e scalată cu înălţimea
deasupra originii piesei, LA PĂTRAT, deci rădăcina nu se mişcă şi vârful se mişcă
cel mai mult. O frunză care alunecă lateral din rădăcină nu e vânt, e un mesh
care se desface.

Faza vine din poziţia instanţei, ca o costişă întreagă de palmieri să nu bată la
unison. Două ritmuri care nu împart perioada, fiindcă unul singur e metronom.

### Trei instrumente greşite înainte de unul bun

„Palmierii se mişcă în vânt" e exact genul de afirmaţie pe care proiectul ăsta o
ia greşit uitându-se la ea, aşa că am scris `tools/png_diff.py` — compară o
CASETĂ din două capturi şi dă un procent.

**Şi primul lucru pe care l-a spus a fost că nu se mişcă nimic:** 18,33% din
caseta cu dealul se schimbă într-o secundă cu plantele îngheţate, 18,02% cu ele
în vânt. Ce se schimba acolo erau umbrele norilor.

Următorul instrument — acelaşi instant, cu sway şi fără — a arătat că mecanismul
merge (3,57% din pixeli deplasaţi). Al treilea, restrâns la pixelii pe care
sway-ul chiar îi mişcă, a ieşit pe dos: cu plantele în mişcare se schimbă MAI
PUŢIN de la cadru la cadru decât cu ele îngheţate. Explicaţia e TAA: netezirea
temporală suprimă tocmai zonele care au vectori de mişcare.

**Concluzia metodologică:** diferenţa de imagine nu poate răspunde „se vede
mişcând?" la două sute de metri. Poate răspunde „e deplasat?", şi atât.

### Sonda care nu lumina

Am legat rampa de înălţime la Emissive ca s-o văd. Nu s-a aprins nimic — nici
catargele. Motivul: scena e în unităţi fizice (soare 110.000 lux), deci un
emissive de 1,0 e o lumânare la amiază. Ridicată la 120.000 de niţi, sonda a
arătat imediat răspunsul: rampa era 1 peste TOATĂ nava, fundul cocii inclusiv,
fiindcă `ObjectBounds` întoarce zero pentru mesh-ul ăla — în timp ce pe plante
rampa mergea corect. Exact genul de „merge pe jumătate" pe care un număr din log
nu l-ar fi arătat niciodată.

Aşa că înălţimea de referinţă e explicită, per instanţă — şi de-aia `MI_Foliage`
s-a despărţit în `MI_Palm` (620 cm) şi `MI_Scrub` (110 cm): îndoirea se
construieşte pe înălţimea proprie a plantei, iar o tufă de un metru cu referinţa
palmierului ar fi rămas practic rigidă (a şasea parte, la pătrat).

### Amplitudinea e fizică, nu grafică

Şase centimetri pe metru pe secundă: la 14 m/s coroana unui palmier de şase metri
face vreo optzeci de centimetri. De la distanţa de la care e privită insula ăsta
e un pixel-doi, **şi aşa şi trebuie** — un palmier care se vede clar mişcând de
la două sute de metri e un palmier care se mişcă greşit. O versiune intermediară
avea 2,2 metri: ştergător de parbriz.

Şi tot greementul se mişcă la fel de mult: un sart e legat de vârful catargului,
deci dacă parâma s-ar mişca de patru ori mai mult decât catargul — ce dădea
citirea „parâmele sunt mai flexibile" — s-ar desprinde vizibil de vergă. Măsurat
pe captură după corecţie: sarturile stau pe catarge.

### Măsurători

Trei chei noi: `sway_mats` (câte materiale dinamice a făcut insula), `sway_wind_ms`
şi `sway_readback_ok` — ultima fiindcă a seta un parametru pe care materialul nu-l
are e tăcut un no-op, exact cum erau cele trei butoane moarte de siaj. Fixtură în
`ci_checks.py` pe linia reală, inclusiv pe cazul `readback FAILED`.

**Task Completed.**

---

## Task Started — 15.09.2026 (ora din zi)

**Prompt:** „continua"
**Model:** Claude Opus 5

Al doilea gol de pe lista onestă: cerul era mereu aceeaşi oră. `-Hour=17.5` mută
acum soarele unde l-ar pune ora aia şi ia cu el culoarea, puterea, lumina cerului
şi banda de expunere.

**Opt-in dinadins.** Fără flag nu rulează nimic şi nivelul îşi păstrează lumina
cu care a fost scris — o oră care ar fi mutat pe tăcute aspectul livrat şi
fiecare număr din linia de bază ar fi fost o schimbare de lumină deghizată în
funcţionalitate.

### Ancora, nu formula

Prima bandă de expunere am calculat-o din principii: luminanţa medie a unei scene
cu albedo 0,2 sub atâţia lux, convertită în EV100. Aritmetica era corectă. Apusul
a ieşit **siluetă neagră pe cer portocaliu** — corect colorat, subexpus cu vreo
două stopuri şi jumătate.

Ce lipsea era ancora. Aspectul livrat e 110.000 lux într-o bandă de 12,5–16 EV,
obţinută acum trei zile prin baleiere şi uitat la capturi. Orice altă oră trebuie
să fie banda AIA mutată cu exact atâţia paşi cu câţi s-a mutat lumina:

    Stops = log2(lux / 110000);  EvMin = 12.5 + Stops;  EvMax = 16 + Stops;

La 17:30 asta dă [10,7 , 14,2] în loc de [14,2 , 16,7] — trei stopuri şi jumătate
diferenţă, şi diferenţa dintre un asfinţit şi o siluetă. **O valoare măsurată
bate o valoare derivată, chiar când derivarea e corectă**, fiindcă derivarea
presupune lucruri despre restul lanţului (tone mapping, expunere, albedo mediu)
pe care nimeni nu le-a măsurat.

### Ce face şi ce nu

Soarele urcă după un sinus între răsărit şi apus şi mătură de la est la vest — nu
e efemeridă, dar are cele două proprietăţi care contează: lumina rade pe apă la
capetele zilei şi vine din alt cadran la fiecare oră. Puterea şi temperatura de
culoare urmează înălţimea (2100 K la orizont, 5800 K sus), fiindcă asta face un
soare jos să se citească drept soare jos.

Sub orizont există o podea: 260 lux şi 11.000 K. Nu e lumină de lună — e „destul
cât să navighezi", fiindcă un cadru negru e ceva ce proiectul ăsta a livrat deja
o dată şi a trebuit să-şi măsoare drumul afară din el.

Norii şi atmosfera rămân cele implicite ale motorului, iar timpul nu curge în
timpul unei partide. Scris în README şi în OWNER_VERIFY 24, ca să nu pară mai
mult decât e.

### Măsurători

Un al zecelea scenariu, `dusk`, fiindcă altfel toată felia s-ar putea şterge şi
fiecare număr din suită ar rămâne identic. Chei noi: `sun_elev_deg`, `sun_lux`,
`sun_kelvin`, `exposure_ev_min` — ultimele două împreună, fiindcă o bandă care
încetează să urmeze lumina se vede aici ca una care se mişcă fără cealaltă.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 1: apartenenţa)

**Prompt:** „deocamdată arată ok, dar, vom imbunatati foarte mult look-ul jocului
si a assets pe viitor, deocamdata vreau sa dezvolti mecanica jocului" + (prin
întrebare) obiective, echipaj + reparaţii, economie; „mai vedem cu abordajul";
autonomie „ca până acum"
**Model:** Claude Opus 5

Grafica e parcată. Pentru stratul de obiective am scris patru designuri
independente şi le-am judecat de două ori — o dată pe fezabilitate faţă de codul
real, o dată pe cum s-ar juca. Ambii judecători au pus primul acelaşi design,
„convoiul", şi ambii au spus că prima lui felie e prea mare. Motivul alegerii:
e singurul ale cărui reguli fac din PARTEA VÂNTULUI pe care stai decizia —
polara măsurată o preţuieşte la ~8:1 — şi singurul care lasă goluri exact pe
forma următoarelor două ateliere (o navă care a coborât pavilionul, o valoare de
marfă, un cronometru de ţinut poziţia).

Commit-ul ăsta e doar piesa cea mai riscantă, singură: `EShipAllegiance
{Player, Crown, Merchant}` pusă de CLASĂ, `IsHostileTo()` într-un singur loc
(`FindTarget`). În lumea cu două tabere „apartenenţa diferă" e aritmetic identic
cu `IsPlayerControlled()`, deci **toate cele 10 scenarii au ieşit nemişcate** şi
n-am reînregistrat nimic. Celelalte patru locuri care iterează nave au fost
recitite şi lăsate deliberat, cu motivul scris la buclă. `-EnemyCount=0` e legal
(verificat: fără victorie la t=0, fără crash).

**Task Completed** (`ed65e93`).

---

## Task Started — 15.09.2026 (mecanica, commit 2: convoiul)

**Prompt:** „continua"
**Model:** Claude Fable 5.1

Convoiul propriu-zis: `AMerchantShipPawn` (a treia tabără), coborârea
pavilionului pe ramura de avarii care deja decide scufundarea, rada,
`-Convoy= -ConvoyNeed= -ConvoyX/Y= -ConvoyWindAngle= -ConvoyRangeM= -RaiderSide=
-RaiderOffingM= -ConvoyStrikeTest=`, o singură linie `CONVOYLOG MISSION` pe
rulare (din toate ieşirile, inclusiv cronometrul de quit), contoare ZĂVORÂTE
gauge/lee/beat eşantionate o dată pe secundă, rândul CONVOY în panou. Fără prăzi,
valoare a mărfii, escortă sau abordaj.

### Perechea care trebuia să iasă diferit a ieşit identic de trei ori

Scenariile `convoy_weather` / `convoy_lee` diferă într-un singur flag. Prima
versiune a dat THROUGH pe ambele părţi, cu 0 salve în vânt. A doua şi a treia la
fel. De fiecare dată jocul nu crăpa, contoarele se mişcau şi orice poartă
„merge" ar fi trecut. Cauzele, în ordinea în care le-am măsurat:

1. **Aceeaşi polară nu poate prinde aceeaşi polară.** Negustorul cu toată pânza
   făcea 6,4 m/s pe un bord larg; raider-ul cobora pe el cu 6,5, iar polara e
   plată între 90 şi 180 de grade. Cu 800 m avans n-a intrat sub 528 m în
   150 s. Cu JUMĂTATE de pânză tot făcea 5,2 — forţa scade cu 1 − v/Vmax şi
   marea dă înapoi cea mai mare parte — deci pânza nu e butonul. Butonul e
   linia de plutire: `LadenSpeedFactor = 0,55` pe `MaxForwardSpeed` → 3,6 m/s,
   şapte noduri contra doisprezece, cât spune epoca.
2. **Căpitanul nu ştia să prindă un fugar.** În raza de angajare vira să pună
   tunurile pe ţintă ORICE ar fi făcut ţinta — corect contra uneia care stă şi
   luptă, dar contra uneia care fuge a stat la 490–540 m două sute de secunde
   fără un foc. Regula nouă: în afara distanţei de menţinere şi fără să câştige
   teren, se duce DREPT spre ea. Prima formă avea histereză pe viteza de
   apropiere şi se răsturna la fiecare cadru (293 de rânduri „running down"):
   fugarul deschide distanţa exact când raider-ul virează să tragă, deci
   histereza trebuie pe DISTANŢĂ (reia la standoff + 150 m, renunţă la
   standoff + 30).
3. **Ţinta sărea.** Recăutarea „cel mai apropiat" la fiecare 2 s: patru
   ghiulele în greementul unui negustor, saltul la celălalt aflat la 150 m,
   trei în al lui, niciunul destul de rănit. Acum ţinta se păstrează până iese
   din luptă; cu o singură cocă duşmană căutarea dă aceeaşi navă, deci nimic
   deja măsurat nu se mişcă.
4. **Dimensionarea.** Cu trei negustori, 800 m şi „opreşte doi", prima salvă
   venea la ~170 s şi convoiul era în radă la 244: de neluat de pe NICIO parte.
   Perechea finală: doi negustori, 1200 m, 600 m avans, opreşte unul.

Rezultatul: în vânt **TAKEN la 71,4 s** (pavilion după două salve în greement,
rig 0,55), gauge 71 / lee 0 / beat 0. Sub vânt **THROUGH la 316,3 s**, gauge 0 /
lee 315 / beat 315, două salve rătăcite, nicio oprire. Testul
`-ConvoyStrikeTest=30` dă TAKEN la 30,0 fără nicio ghiulea, iar raider-ul AI o
lasă în pace (`AILOG target lost`).

### Mutaţii

Arborele e necomitat, deci restaurarea NU s-a putut face prin git: copie
pristină → mutaţie (cu asert că textul s-a schimbat) → build → rulare →
restaurare din copie → comparaţie bit cu bit. (A) pragurile de pavilion la 0: 7
lovituri, 0 opriri, THROUGH la 321. (B) urmărirea dezactivată: 0 salve, THROUGH
la 316. (C) pristin din nou: TAKEN la 71,4, aceleaşi numere — reproductibil.

### Suita

Cele 10 scenarii vechi: **niciun număr mişcat**. Chei noi: `pursuit_ticks_max`
peste tot, plus cele două scenarii. De spus fără ocoliş: regula de urmărire a
TRAS 248 de ticuri în `sailing` şi 291 în `crowded` (jucătorul face cercuri sub
`-ShipRudderTest`), şi nimic măsurat acolo nu s-a mişcat — nu fiindcă regula ar
fi echivalentă, ci fiindcă nimic măsurat acolo nu se uită la drumul căpitanului.
Nemişcat şi neobservat nu sunt acelaşi lucru. Linia de bază reînregistrată în
acelaşi commit, doar cu adăugiri.

`ci_checks.py` citeşte acum liniile MISSION / STRUCK / pursuitTicks din fixturi
copiate din log, şi verifică că STRUCK nu se numără ca SUNK. README are secţiunea
„Convoiul", OWNER_VERIFY punctul 25 (un negustor oprit ARATĂ oprit?), HANDOFF
regulile 7 şi 8.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 3: echipajul, felia 1)

**Prompt:** „continua"
**Model:** Claude Fable 5.1

Oameni, nu doar lemn. `Hands` (60; negustorul 14) şi `Casualties` zăvorât pe
`AShipPawn`; o ghiulea omoară doi în cocă, trei la un tun scos, unul sus sau la
cârmă, niciunul la eşuare. `RepairShare` (R ciclează 0 → ¼ → ½; sau
`-ShipRepairShare=`) împarte oamenii rămaşi între tunuri şi reparaţii: tunurile
se reîncarcă cu `HandsOnGuns / 48` (opt tunuri a câte şase), deci cu şaizeci la
bord primii doisprezece morţi nu costă nimic şi fiecare de după încetineşte
reîncărcarea, până la un sfert; echipa de reparaţii înnoadă cârma întâi, apoi
catargul mai rău, cu 0,00015 pe om pe secundă, până la plafonul de 0,85. Coca nu
se repară pe mare. Căpitanul inamic trimite jumătate din oameni sus când e
rănit şi n-are în ce trage (`-AIRepair=0` opreşte doctrina). Bara HANDS în
panou, `CREWLOG` per lovitură şi o linie per cocă la quit, `t=` la capătul
liniei de salvă.

### Perechea: `crew_repair` / `crew_fight`

Un inamic cu greementul la 0,40 la 1,5 km de jucătorul care stă; un singur flag
diferă. Cu reparaţii ajunge cu 0,77 din greement şi trage prima salvă la
**209,5 s**; fără, ajunge cum a plecat şi trage la **239,6 s**. `repaired_max`
0,748 vs 0.

### Ce a ieşit la iveală: căpitanul nu ştia să tragă de la 380 m

Prima versiune a perechii a ieşit pe dos: cu reparaţii ajungea mai repede şi
**nu trăgea deloc**, fără reparaţii trăgea de şase ori. Nu era echipajul: în
afara distanţei de menţinere unghiul de „uşurare" era un sfert de grad pe metru,
deci la 380 m — 15°, un drum la 75° de relevment. Contra unei nave staţionare
asta e un cerc lent la 380 m cu ţinta la 15° de travers, iar arcul de tragere e
9°. Nouăzeci de secunde fără o salvă, apoi cercul a dus-o cu prova în vânt şi a
stat în irons până la sfârşit. Nava mai lentă (0,40) trăgea fiindcă nu ţinea
drumul aşa de bine şi ţinta îi nimerea în arc — noroc geometric, nu tactică.

Prima reparaţie — câştig de trei ori mai mare în afara distanţei de menţinere —
a reparat perechea şi **a stricat convoiul**: raider-ul care urmărise negustorul
până la 350 m îşi termina virajul cu ţinta la 22° de travers în loc de 7, nu mai
trăgea niciodată, şi convoiul luat la 71 s trecea. A doua: o **bandă de tragere**
între distanţa de menţinere şi +60 m în care drumul e travers-pe-ţintă, câştigul
mare doar dincolo de ea, iar înăuntrul distanţei de menţinere nimic schimbat —
deci familia `gunnery`, care porneşte înăuntru, e neatinsă. Convoiul: TAKEN la
70,9.

### Capcane de unelte, plătite azi

`-EnemyRigDamage=0.4` neghilimetat în PowerShell ajunge `0` — prima pereche a
pornit dezarborată complet, nu la 0,40, şi „mergea". Şi `python - @'...'@` în
PowerShell deschide un REPL şi atârnă până la timeout — regula era deja în
memorie; scriptul de analiză e acum fişier.

### Mutaţii şi suită

Mutaţii, restaurate din copie pristină şi comparate pe conţinut: (A) nimeni nu
moare — `casualties_max` cade de la 8 la 0 în `gunnery`, iar în `crew_fight`
factorul de reîncărcare urcă la 1,00; (B) dulgherul nu face nimic — perechea
se prăbuşeşte la IDENTIC (prima salvă 239,6 = 239,6, reparat 0); (C) pristin —
0,748 / 209,5 din nou.

Suita: familia `gunnery` (gunnery, gale, sinking, carried/loose_shot),
`lee_shore` şi `dusk` **nemişcate**. Au mutat, toate din drumul de apropiere al
căpitanului: `convoy_weather` (tot TAKEN, 70,9 în loc de 71,4, cinci salve în
loc de trei), `grounding` (inamicii care vin peste jucătorul eşuat trag două
salve în loc de una) şi `pursuit_ticks_max` din `sailing`/`crowded`, de la
248/291 la **0** — cu banda de tragere, apropierea se face suficient de repede
ca regula de urmărire să nu mai aibă de ce să tragă contra unui jucător care
face cercuri. Chei noi peste tot: `casualties_max`, `gun_crew_min`,
`repaired_max`, `enemy_rig_quit`, `first_broadside_t`. Linia de bază
reînregistrată în acelaşi commit; ce a mutat e scris aici, nu doar în diff.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 4: prăzile)

**Prompt:** „continua"
**Model:** Claude Opus 5

Economia începe aici, şi începe cu un preţ pus pe o alegere care exista deja şi
nu costa nimic.

### Designul: trei propuneri, doi judecători

Ca la arcul de obiective: trei designuri independente pentru economie
(**prăzi**, **negoţ cu magazie şi socoteală**, **progresie între misiuni**),
fiecare scris contra codului real, apoi judecate de două ori — o dată pe
fezabilitate, o dată pe cum s-ar juca. **Ambii judecători au clasat primul
acelaşi design (prăzile) şi ambii au spus că prima lui felie e prea mare, tăind-o
în acelaşi loc:** valoarea se încasează în clipa coborârii pavilionului, fără
stăpânire, fără echipaj de pradă, fără timp de acostare, fără stare nouă de AI.

Argumentele care au decis, amândouă verificabile în arbore:
- **E singura al cărei levier e deja în mâna jucătorului.** Shift e legat de
  tirul înalt din prima zi şi până acum nu însemna nimic: sus o dezarborezi
  lent, jos o scufunzi repede, ambele se termină în acelaşi jeton.
- **Celelalte două măsurau plafonul, nu mecanica.** La „negoţ", socoteala e
  dominată de două constante fixate în linia de comandă, iar cheia perechii
  („a rămas fără ghiulele") e `mission_result` cu perucă. La „progresie",
  `purse_end` e identic `240 × convoy_stopped` — o redenumire afină a unei chei
  care există deja.

### Ce s-a construit

`valoare = marfă × (cocă rămasă / cocă întreagă)`, încasată în
`HandleShipStruck`, care deja doar numără şi scrie (constrângerea de callback
fizic e respectată). Marfa e 1200 (`-ConvoyCargo=N`). `Purse`, `PrizesTaken`,
`PrizeValueMax` zăvorâte pe game mode. O linie `PRIZELOG` per pradă cu
**ingredientele lângă rezultat** (`value=1200 cargo=1200 hull=1.00 rig=0.55
zone=rig`) şi o linie `PURSE` la fiecare quit, inclusiv în rulările fără convoi
— un zero numărat. Rândul PURSE în panou.

Singurul flag nou pe căpitan, `-AIAimHigh=1|0`, **mută knob-ul existent**
`FireHighAboveRig` la -1 sau 2, fără nicio ramură nouă la punctul de tragere: o
a doua cale de a decide acelaşi lucru ar fi fost o a doua sursă de adevăr.

### Perechea

`prize_rig` / `prize_hull` diferă într-un singur flag:

| | pavilion la | cocă la încasare | greement | plăteşte |
|---|---|---|---|---|
| tir înalt | 70,9 s | 1,00 | 0,55 | **1200** |
| tir în cocă | 83,1 s | 0,58 | 1,00 | **696** |

Cifrele NU sunt alese: judecătorul de fezabilitate le-a derivat înainte de
rulare din constantele din arbore (`ImpactDamage = 60`, `StrikeBelowFraction =
0.6` → ~7 ghiulele → cocă ~0,58), şi măsurătoarea a dat 0,58 şi 696.
`prize_rig_at_take` e **momeala**: se mişcă invers (0,55 contra 1,00), fiindcă o
formulă legată din greşeală de greement ar fi făcut perechea să difere tot — dar
în ordinea cealaltă.

### Instrumentul care citea coca greşită

Grefat din designul care a pierdut, fiindcă avea dreptate: `gun_crew_min` citeşte
**0,25 în ambele scenarii de convoi** — nu raider-ul, ci negustorul cu 14 oameni
care stă pe `MinGunCrewFactor` din primul tick. Un minim peste toate cocile nu
poate raporta niciodată coca despre care întrebi. Adăugat `enemy_gun_crew_quit`,
citit DUPĂ NUME de pe linia ei, exact lecţia care a produs `enemy_rig_quit`.

Şi `purse_balances`: fixtura re-derivă suma din liniile per-pradă, deci o
greşeală de transcriere în bani iese ca un boolean care s-a MIŞCAT, nu ca o cifră
pe care trebuie s-o vadă cineva cu ochiul.

### Mutaţii şi suită

Mutaţii, restaurate din copie pristină şi comparate bit cu bit (arborele e
necomitat, git nu putea): (A) valoarea ignoră coca — perechea se prăbuşeşte,
ambele plătesc 1200; (B) flag-ul nu e citit — ambele jumătăţi cad pe doctrina
căpitanului şi devin identice până şi la secundă (70,9 = 70,9), ceea ce
dovedeşte că FLAG-UL le separă, nu altceva din cele două linii de comandă; (C)
pristin — 1200 contra 696 din nou.

Detaliu care merită spus: la (A), cheia banilor se aplatizează, dar
`prize_hull_at_take` (1,00 / 0,58) şi `first_strike_t` (70,9 / 83,1) rămân
diferite. Adică instrumentele spun „mecanica s-a rupt", nu „scenariul a
degenerat" — exact distincţia pe care o pierde o pereche cu o singură cheie.

**Suita: NICIUN număr mutat** în cele 14 scenarii existente. Numai chei noi
(`purse_end`, `prizes_taken`, `prize_value_max` peste tot ca zerouri numărate;
`prize_hull_at_take`, `prize_rig_at_take`, `prize_strike_zone`,
`purse_balances` unde există o pradă; `enemy_gun_crew_quit` peste tot) şi cele
două scenarii noi. Asta e aceeaşi afirmaţie pe care a făcut-o commit-ul
apartenenţei, şi e cea mai puternică dovadă pe care o poate purta o felie
tăiată: nimic din ce mergea nu s-a clintit.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 5: stăpânirea prăzii)

**Prompt:** „Continua"
**Model:** Claude Opus 5

O navă care a coborât pavilionul e **oprită**, nu **a ta**. Felia asta e ce costă
să fie a ta: te apropii la 150 m, stai lângă ea douăzeci de secunde **cumulate**,
şi pleacă doisprezece oameni la bordul ei pentru totdeauna.

Acolo muşcă felia de echipaj, fără niciun flag de reglaj: 60 de oameni la bord,
48 la tunuri pentru reîncărcare plină, deci **prima pradă e gratis şi a doua nu**.
Măsurat cu valorile implicite: raider-ul ia ambii negustori, pleacă 24 de oameni,
reîncărcarea scade la 36/48 = 0,75.

### Ce am luat din avertismentele judecătorilor, cuvânt cu cuvânt

Recenzia de design de la commit-ul trecut a numit trei capcane în felia asta
înainte să fie scrisă, şi toate trei erau reale:

1. **Doctrina implicit APRINSĂ ar fi detonat convoiul.** Azi, când un negustor
   coboară pavilionul, căpitanul îşi pierde ţinta şi se duce la următorul — aşa e
   măsurată toată familia de convoi. `bTakesPrizes` e **implicit stinsă**; doar
   cele două scenarii noi o aprind. Rezultat: **zero numere mutate** în cele 16
   scenarii existente.
2. **Oamenii de pradă NU trec prin `LoseHands()`.** Nu sunt morţi. Ar fi dat unei
   variabile două meserii şi ar fi mutat `casualties_max`, o cheie pinată în
   familia de convoi, dintr-un motiv care n-are legătură cu tirul. Consecinţa e
   scrisă în cod, nu descoperită mai târziu: **după ce pleacă o echipă de pradă,
   `Hands + Casualties` nu mai e `HandsMax`** — oamenii lipsă sunt în
   `HandsInPrizes`, şi linia PURSE îi arată ca să se închidă socoteala.
3. **Timpul alături se ACUMULEAZĂ, nu se resetează.** Un „dwell" neîntrerupt ar
   fi cerut o ţinere de poziţie la o distanţă pe care căpitanul ăsta n-a fost
   niciodată pus s-o ţină (`StandoffM` e 320 m), şi ar fi eşuat tăcut — cu
   `prizes_manned` zero în ambele jumătăţi şi o pereche care nu măsoară nimic.

### Numărul care a ales distanţa de acostare

`prize_closest_m`, scris în FIECARE rulare, cu doctrina aprinsă sau stinsă. Cu
ea stinsă, cât de aproape ajunge raider-ul natural de o navă care a coborât
pavilionul e **272 m** — de trei ori distanţa de acostare. Fără doctrină nu s-ar
lua niciodată o pradă, şi asta nu se putea şti presupunând.

### Perechea, şi podeaua

Perechea e `prize_rig` (doctrina stinsă) contra `prize_manned` (aprinsă) —
aceeaşi linie de comandă, un singur flag. Nu am adăugat un scenariu redundant.

| | stăpânite | oameni plecaţi | reîncărcare | ticuri lângă pradă | pungă |
|---|---|---|---|---|---|
| doctrina stinsă | 0 | 0 | 1,00 | 0 | 1200 |
| doctrina aprinsă | 2 | 24 | 0,75 | 8501 | 2400 |

Şi o **podea**: sub douăzeci de oameni pe punte bărcile nu mai pleacă.
`prize_shorthanded` o măsoară determinist cu `-EnemyHands=28` (flag PĂZIT pe
apartenenţă — blocul în care stă e ramura `else` a lui `IsPlayerControlled()`,
în care cad şi negustorii).

**Cum am ajuns acolo e partea care merită scrisă.** Întâi am încercat podeaua cu
`-PrizeCrew=25` la 400 s: `prizes_refused` a citit 0. Am întins la 500 s: tot 0.
Cauza nu era timpul — trimiterea a 25 de oameni îi încetineşte tunurile (35/48),
deci al doilea negustor a coborât pavilionul la 352 s în loc de 177, iar până
atunci raider-ul rămăsese la 350 m **sub vânt** de ea, ceea ce polara proiectului
preţuieşte la sute de secunde. **Întinderea rulării nu repară o geometrie
greşită.** Refuzul s-a făcut determinist, nu răbdător.

### Un contor care creştea degeaba

Refuzată, raider-ul rămânea lângă pradă până la finalul rulării: 13.224 de ticuri
de nimic. Un contor care creşte cât timp nava nu realizează nimic măsoară
lungimea rulării. Acum renunţă după patruzeci de secunde alături degeaba şi face
vela — 5.792 de ticuri, iar o luare normală rămâne neatinsă (8.501: pragul nu se
declanşează niciodată într-o luare care merge).

### Poarta a ieşit roşie, şi avea dreptate

Linia PURSE a crescut cu patru câmpuri, iar cele trei fixturi scrise la
commit-ul trecut au ieşit **roşii** până le-am adus la linia pe care jocul o
scrie acum. Exact defectul pentru care există fişierul: un cititor care încetează
tăcut să potrivească o linie pe care o citea.

### Mutaţii şi suită

Mutaţii, restaurate din copie pristină şi comparate bit cu bit: (A) oamenii sunt
gratis — `prize_hands_out` 24 → 0 şi reîncărcarea 0,75 → 1,00, dar
`prizes_manned` RĂMÂNE 2. Adică „prada a fost luată" şi „prada a costat" sunt
separate, ceea ce o singură cheie nu poate face. (B) fără podea —
`prizes_refused` 1 → 0, prada e luată în schimb, şi raider-ul coboară la 16
oameni cu tunurile la 0,33: exact ce există podeaua să oprească. (C) pristin —
totul la loc.

**Suita: ZERO numere mutate** în cele 16 scenarii existente. Chei noi:
`prizes_manned`, `prizes_refused`, `prize_hands_out`, `prize_closest_m`,
`prize_ticks_max`.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 6: portul)

**Prompt:** „continua cu commit 2" → întrebat, owner-ul a ales **portul (punga să
cumpere ceva)** din trei variante
**Model:** Claude Opus 5

Portul, felia 1: **acolo unde o pradă devine bani şi oamenii se întorc.**

O pradă cu echipajul tău la bord face vela şi fuge spre o radă prietenă pe exact
acelaşi drum pe care un negustor îl navighează spre radă — fiindcă exact asta e:
un negustor cu altă destinaţie. Zero comportament nou: `TickMerchant` +
`SetDestination`, amândouă deja măsurate.

Rada e aşezată implicit **sub vântul** convoiului, şi nu din decor: o pradă e
lucrată de doisprezece oameni acolo unde şaizeci o navigau, iar doisprezece
oameni nu duc o cocă încărcată în vânt. Polara proiectului o spune, aşezarea o
respectă.

### Două cifre, nu una

`PURSE` e cât valorau prăzile când au coborât pavilionul. `LANDED` e cât a ajuns
la chei. Sunt egale doar dacă toate au ajuns acasă — şi din două prăzi luate, în
cinci sute de secunde **una** ajunge. Cealaltă e pe mare când se termină partida,
şi aia e o pierdere adevărată.

Perechea `prize_home` / `prize_noport`, un singur flag:

| | prăzi ajunse | la chei | oameni întorşi | tunuri la quit |
|---|---|---|---|---|
| fără port | 0 | 0 | 0 | 0,75 |
| cu port | 1 | 1200 | 12 | **1,00** |

`purse_end` e IDENTIC în ambele (2400) — şi trebuie să fie: valoarea se
stabileşte la pavilion, iar un port care ar schimba-o ar însemna că nu fusese
încasată acolo unde commit-ul trecut spune că a fost. Iar reîncărcarea care se
întoarce la 1,00 e mecanica plătind a doua oară, într-un loc independent: cei
doisprezece oameni întorşi duc tunurile înapoi la 48.

### Capcana în care am căzut, deşi era scrisă

Verificarea „a ajuns prada în radă?" am pus-o prima dată în `SampleWeatherGauge`,
lângă rada negustorilor, fiindcă e aceeaşi întrebare pusă celeilalte tabere.
**N-a rulat niciodată.** Funcţia aia iese pe `bMissionOver`, iar `FinishMission`
îi şterge timerul — convoiul se decide la 71 s, prada are nevoie de 356.

Recenzia de design de acum două commit-uri identificase exact capcana asta la un
alt design („e moartă prin construcţie"), şi tot am intrat în ea. Ce a salvat-o e
instrumentul: logul a spus-o într-un singur rând — o pradă la **şase metri** de
chei lângă `landed=0`. Verificarea a mutat pe timerul prăzilor, cel care nu se
şterge niciodată, şi care pentru asta există.

### Un defect pe care l-a prins suita, nu eu

`prize_manned` — scenariu FĂRĂ port — şi-a mişcat două numere de siaj. Cauza: un
negustor primeşte destinaţia (rada lui) la naştere, iar coborârea pavilionului
nu i-o ia. Deci o pradă luată într-o lume fără port pleca spre **rada
inamicului**, cu doisprezece oameni de-ai tăi la bord. Reparat la cauză:
`ClearDestination()` când nu există port.

Două numere de siaj într-un scenariu care n-are nicio legătură cu portul au fost
tot ce a ieşit la suprafaţă. Fără suită, ar fi fost o navă care pleacă în direcţia
greşită şi nimeni n-ar fi ştiut.

### Un câmp care ar fi devenit mincinos

`handsOut=` însemna „oameni trimişi vreodată", şi se citea „oameni plecaţi
acum". Cât timp nimeni nu se întorcea, erau acelaşi număr. Portul le-a despărţit.
Redenumit `handsSent=`, cu `handsHome=` lângă el, amândouă monotone, iar „plecaţi
acum" se derivă. Cheia din linia de bază s-a redenumit la fel, ceea ce poarta a
raportat ca 18 măsurători care „au încetat să fie luate" — exact ce trebuie să
raporteze.

### Mutaţii, şi una care a găsit instrumentul, nu codul

Restaurate din copie pristină şi comparate bit cu bit: (A) prada nu navighează —
rămâne unde a fost luată, `prizes_landed` 1 → 0, perechea se prăbuşeşte; (C)
pristin — totul la loc.

(B) **oamenii nu se mai întorc** a fost interesantă. Prima oară, `prize_hands_home`
a citit tot 12. Nu codul era în regulă — **cheia era**: număra oamenii care ajung
la CHEI, sub un nume care promitea oameni ajunşi pe o PUNTE. Cât timp cele două
erau acelaşi număr, nimeni n-avea cum să vadă. Cauza era că pawn-ul îşi seta
contoarele într-o instrucţiune şi raporta apelantului în alta, deci o mutaţie
care o scotea pe prima o lăsa pe a doua să spună că doisprezece oameni s-au
întors la o navă care nu i-a primit.

Reparat prin factorizare: `TakeBackPrizeCrew()`, oglinda lui `DetachPrizeCrew()`
— **o funcţie, un adevăr**. Acum mutaţia dă exact ce trebuie: prada ajunge la
chei (1200 încasaţi), zero oameni se întorc, tunurile rămân la 0,75.

Suita: **zero numere mutate**. Cele 18 „au încetat să fie măsurate" sunt
redenumirea deliberată `prize_hands_out` → `prize_hands_sent`; restul sunt chei
noi (`prizes_landed`, `purse_landed`, `prize_hands_home`) şi cele două scenarii.

**Ce NU face:** punga tot nu CUMPĂRĂ nimic. Portul face banii reali şi aduce
oamenii înapoi; tunuri, oameni noi şi reparaţii de cocă pe bani sunt felia
următoare. Şi nimeni nu încearcă să recaptureze o pradă pe drum.

**Task Completed.**

---

## Task Started — 15.09.2026 (un executabil care porneşte jocul)

**Prompt:** „ok, vreau un executabil care sa porneasca jocul"
**Model:** Claude Opus 5

`Packaged\Windows\PirateSeas.exe` — build de sine stătător, 0,84 GB, care nu are
nevoie de Unreal instalat. Două scurtături lângă celelalte proiecte, în
`MyWork\Apps\games`, amândouă cu `-windowed`: un build care porneşte pe tot
ecranul şi prinde mouse-ul e cel mai prost prim lucru pe care i-l dai cuiva.

`Development`, nu `Shipping`, fiindcă păstrează logul şi flag-urile de linie de
comandă — **executabilul ia exact aceleaşi flag-uri ca editorul**: `-Convoy=`,
`-Port=`, `-AIPrize=`, `-ShipQuitAfter=`, tot tabelul.

### Prima împachetare a picat, şi a scos la iveală ce tolera editorul de luni

Cook-ul **numără erorile şi refuză să producă un build**. Editorul doar le derula
pe ecran. Erau opt, în două feluri, şi amândouă existau în FIECARE rulare de până
acum — inclusiv în toate cele de pe care e construită linia de bază:

1. **Şapte `GetSimplePhysicalMaterial: GEngine not initialized`.** Constructorii
   chemau `SetSimulatePhysics`, `SetMassOverrideInKg`, `SetCenterOfMass` şi
   `SetUseCCD` — apeluri care recalculează proprietăţile de masă şi cer
   materialul fizic, în timp ce se construieşte obiectul implicit al clasei,
   înainte să existe motorul. Scrise acum direct pe `BodyInstance`, care pune
   exact aceleaşi câmpuri fără drumul de runtime.
2. **Lipsea profilul de coliziune al apei.** Plugin-ul Water şi-l adaugă singur
   în `DefaultEngine.ini` — dar numai când îl porneşti din INTERFAŢA editorului.
   Proiectul ăsta e făcut integral prin script şi n-a deschis niciodată
   interfaţa aia. Deci apa a mers de la bun început pe un comportament de
   rezervă, iar `ci_measure.py` avea scris negru pe alb că „editorul iese cu 1
   la fiecare rulare, deci codul de ieşire nu dovedeşte nimic".

### Care din cele două a mutat numerele

Amândouă au intrat odată şi **24 de măsurători s-au mişcat**. Să dau vina pe una
prin raţionament ar fi fost o ghicitoare, aşa că am scos DOAR modificarea din
`.ini` şi am rulat suita:

**Zero mutate.** Deci mutarea apelurilor pe `BodyInstance` nu schimbă nimic —
e curată — iar toate cele 24 vin de la apă, care acum se comportă cum cere
plugin-ul. **Linia de bază veche descria comportamentul de rezervă.** Cele mai
multe mişcări sunt de un cadru sau o unitate (`pursuit_ticks` 2619→2620,
`first_broadside_t` 6,7→6,8); câteva sunt reale (în `gunnery` o ghiulea care
lovea acum face stropi: `struck` 10→9, `splashes` 6→7).

### Ce am câştigat pe lângă executabil

Un log cu **zero erori de orice fel**, prima dată în proiect — şi, ca urmare,
editorul iese acum cu **0**. Aşa că poarta de măsurare citeşte de acum codul de
ieşire şi crapă dacă nu e zero. Un comentariu care spunea de luni „asta nu
dovedeşte nimic" a devenit o verificare care poate ieşi roşie.

### Verificarea executabilului

Nu „s-a deschis o fereastră". Pornit cu propriile lui flag-uri şi citit din
propriul lui log, care NU e în proiect ci lângă exe, în
`Packaged\Windows\PirateSeas\Saved\Logs\` (am căutat întâi în `%LOCALAPPDATA%`
şi n-am găsit nimic): harta încărcată, game mode-ul nostru viu, marea
construită, nava care pluteşte (**1,002 medie pe 90 de eşantioane**, acelaşi
număr ca în editor), panoul care desenează, tunurile care trag, ieşirea pe
`-ShipQuitAfter=`. Şapte din şapte, zero erori fatale.

**Task Completed.**

---

## Task Started — 15.09.2026 (mecanica, commit 8: punga cumpără)

**Prompt:** „continua cu punga sa cumpere"
**Model:** Claude Opus 5

Portul vinde exact cele două lucruri pe care marea nu le dă înapoi: **oameni**
(morţii nu se întorc, cei plecaţi cu o pradă sunt plecaţi) şi **cocă** (echipele
de reparaţii fac cârma şi catargele şi n-au voie la cocă — scria în cod de două
commit-uri).

Un om 20, un punct de cocă 0,5. O cocă întreagă e 500, mai puţin de jumătate
dintr-o pradă; echipajul de pradă de doisprezece oameni costă 240 să-l
înlocuieşti. **Deci o pradă dusă acasă plăteşte de două ori oamenii care au
luat-o, şi una scufundată nu plăteşte nimic.** Se plăteşte din `Landed`, nu din
`Purse` — banii ajunşi la chei, nu cei revendicaţi.

Şi nu e instantaneu: un om sau douăzeci de puncte la fiecare jumătate de secundă
în radă. Banii sunt un cost, ceasul misiunii e celălalt.

### Bucla se închide

`prăzile costă oameni → portul face banii reali → banii cumpără oamenii înapoi`

Măsurat cap-coadă, într-o singură rulare: pradă luată la 138 s, acasă la 359 cu
1200 în vistierie, căpitanul bate spre port, ajunge la 764, cumpără **20 de
oameni şi 400 de puncte de cocă pentru 600**, iese în larg la 776 cu 600 rămaşi.
Aritmetica e pe linia de log şi fixtura o verifică: 20×20 + 400×0,5 = 600.

### Regula care a lipsit prima dată

Prima versiune a doctrinei spunea „eşti lovit → du-te în port". Măsurat: o navă
care pleca lovită bătea spre port **în primele secunde**, înainte să fi câştigat
un ban, şi stătea într-o radă goală toată partida — fără să vâneze, fără să
câştige, fără să cumpere. Regula corectă e evidentă odată văzută: **te duci în
port când eşti lovit ŞI ai cu ce plăti.** O călătorie pe care n-o poţi plăti e o
călătorie pierdută.

Mutaţia B o pune la loc şi o dovedeşte: fără ea, raider-ul petrece **47.971 din
48.000 de ticuri** ale rulării într-o radă goală.

### Un defect vechi scos la iveală de scenariu

Regula de renunţare la o pradă cerea să fie *alături* de ea. Într-o rulare a
stat **305 secunde** — jumătate de partidă — lângă o pradă la care nu putea
ajunge, în timp ce portul unde avea bani de cheltuit rămânea nevizitat. Acum
renunţă şi după 180 de secunde de încercare totală, indiferent dacă a ajuns
vreodată alături. Trei minute e mai mult decât orice luare reuşită măsurată
vreodată aici (cea mai lentă: 142 s), deci nu poate întrerupe una.

### Un flag care însemna altceva decât părea

`-EnemyHands=N` seta şi COMPLEMENTUL navei, nu doar câţi sunt la bord — deci era
o navă mică, nu una cu echipaj incomplet, şi `GetHandsShort()` ieşea zero. Cu
asta, `handsBought` era o cheie care nu putea ieşi nenulă în niciun scenariu.
Acum înseamnă câţi sunt la bord, complementul rămâne 60, şi portul are ce
completa. Verificat: nu mişcă niciun număr din `prize_shorthanded`, care
foloseşte flag-ul pentru cu totul altceva.

### Dimensionarea, din cronologie măsurată

Scenariile rulează 800 s fiindcă am citit cronologia o dată, nu fiindcă sună
bine: 359 acasă, 539 renunţă la a doua pradă, 764 ajunge în radă, 776 gata. Mai
scurt, şi rularea s-ar termina cu ea încă pe drum — ceea ce s-ar citi ca „refitul
nu face nimic".

### Mutaţii şi suită

(A) nimeni nu poate fi recrutat — oamenii cumpăraţi cad la 0 dar coca rămâne
400, ceea ce **separă cele două cumpărături**; (B) fără regula vistieriei — vezi
mai sus; (C) pristin — 600 / 20 / 400 / 600 din nou.

Perechea `refit_on` / `refit_off`, un singur flag: 600 cheltuiţi contra 0, 20 de
oameni contra 0, 400 de cocă contra 0, vistierie 600 contra 1200. Iar ce NU
trebuia să difere nu diferă: `purse_end` 2400 şi `purse_landed` 1200 în ambele —
câştigă la fel, doar una cheltuie.

**Suita: zero numere mutate** în cele 20 de scenarii existente.

**Task Completed.**

---

## Task Started — 16.09.2026 (mecanica, commit 9: magazia)

**Prompt:** „continua"
**Model:** Claude Opus 5

Ghiulelele se termină. `-Shot=N` / `-EnemyShot=N` umplu o magazie reală; **fără
flag e fără fund**, adică exact cum a mers jocul până acum — şi de aceea niciun
număr din cele 22 de scenarii existente nu se mişcă.

O salvă cheltuie câte o ghiulea de tun. Dacă n-ai câte una pentru fiecare tun
care mai trage pe bordul ăla, salva nu pleacă deloc şi nu-ţi arde nici
reîncărcarea. Bara SHOT în panou, avertisment MAGAZINE DRY, portul vinde ghiulele
cu 2 bucata şi le cumpără PRIMELE la refit — un echipaj întreg pe o cocă sănătoasă
cu magazia goală e un transport, nu o navă de luptă.

### Garda e înainte de buclă, şi e totul-sau-nimic

Recenzia de design de acum câteva commit-uri numise capcana înainte să fie
scrisă: `FireBroadside` e locul cel mai sensibil la sămânţă din proiect, fiindcă
bucla trage **două numere aleatoare per tun** (deriva şi înălţimea) din şirul pe
care `-ShipSeed` îl fixează. Un tun care ar refuza tăcut să tragă ar sări peste
tragerile lui şi ar muta FIECARE ghiulea de după el în rularea aia — şi orice
comparaţie înainte/după ar citi sămânţa în loc de schimbare.

Deci verificarea stă **înaintea** buclei şi e totul-sau-nimic: ori pleacă toată
salva, ori niciuna, iar bucla de dedesubt e bit cu bit cea dinainte. Proba că a
mers: **suita a ieşit nemişcată.**

### Contorul se numără pe reprize, nu pe cadre

AI-ul cere să tragă la FIECARE tick în care tunurile ei poartă. Un contor de
refuzuri pus acolo ar număra cadre. Mutaţia B îl deblochează şi citeşte **1197
în loc de 1** — un număr care ar fi arătat plauzibil şi ar fi însemnat „rata de
cadre", nu „de câte ori a rămas fără".

### Perechea schimbă deznodământul

`magazine_dry` / `magazine_full`, un singur flag:

| | trase | rămase | uscată | misiune |
|---|---|---|---|---|
| 4 ghiulele | 4 | 0 | 1 | **A TRECUT** |
| 40 de ghiulele | 20 | 20 | 0 | **LUAT** |

Cu o singură salvă în magazie nu poate aduce niciun negustor la pavilion, şi
convoiul scapă. Nu e contabilitate, e rezultatul.

### Mutaţii

(A) magazia nu se termină niciodată — `magazine_dry` devine identic cu
`magazine_full` (20 trase, 0 uscată, LUAT) şi perechea moare; (B) contorul
nezăvorât — 1197; (C) pristin — 4/1/A TRECUT contra 20/0/LUAT.

### O întrebare pentru owner, nu o verificare

OWNER_VERIFY 31 e prima intrare care cere o DECIZIE, nu o privire: **ar trebui
magazia să fie finită implicit?** Pot măsura tot ce face; nu pot şti dacă face
jocul mai bun sau doar mai enervant. Şi e o schimbare care nu se poate face pe
jumătate — ar muta fiecare număr de tir din linia de bază odată, şi toate
scenariile de până acum ar descrie alt joc.

**Task Completed.**

---

## Task Started — 16.09.2026 (magazia devine finită implicit)

**Prompt:** „da, fa magazia finita implicit"
**Model:** Claude Opus 5

Owner-ul a răspuns la întrebarea din OWNER_VERIFY 31. Magazia e acum **finită
implicit: patruzeci de ghiulele, zece salve, cinci pe bord.**

### Numărul vine din suită, nu din gust

Înainte să aleg, am citit ce trage fiecare scenariu — cheia `shot_fired` exista
deja din commit-ul trecut, pentru toate cele 24 de scenarii, chiar şi acolo unde
magazia era fără fund. Inamicul trage:

| | ghiulele |
|---|---|
| aproape toate scenariile | 0–20 |
| `crew_repair` (urmărire lungă) | 24 |
| **`crew_fight` (duel de 360 s)** | **44** |

Patruzeci acoperă orice acţiune scurtă şi se goleşte exact în singurul loc unde
o magazie ar trebui să conteze. Orice număr peste 44 n-ar lega nicăieri în suită
— şi **un implicit care nu leagă nicăieri nu se poate deosebi de lipsa lui.**

### O linie care ar fi făcut defectul să arate ca funcţionalitate

`Shot` porneşte de la zero. Cu un `ShotMax` implicit şi fără flag, fiecare navă
ar fi pornit **uscată din primul tick** — şi asta ar fi arătat exact ca „magazia
finită funcţionează". Un `Shot = ShotMax` la BeginPlay, cu motivul scris acolo.

Negustorii primesc `ShotMax = 0`: n-au tunuri, deci n-au magazie.

### Ce s-a mutat, şi predicţia pe care am ratat-o

Am scris înainte de rulare că singurul scenariu care ar trebui să se mişte e
`crew_fight`. **Am avut dreptate pe jumătate.** Din 51 de mişcări:

- **43 sunt contabilitate pură** — cheile `shot_left` şi `shot_max`, care până
  acum citeau zero peste tot şi acum citesc valori reale.
- **8 sunt comportament, în DOUĂ scenarii:**
  - `crew_fight`, exact cum am prezis: 44 → 40 ghiulele, 11 → 10 salve, o
    refuzare pe uscat. A unsprezecea salvă nu mai pleacă.
  - `refit_on`, pe care l-am ratat: raider-ul **cumpără acum 20 de ghiulele cu
    40** la refit, fiindcă portul le vinde şi le cumpără primele. Asta nu e un
    defect, e bucla care se închide — dar n-am prevăzut-o, şi diferenţa dintre
    „am prezis" şi „am înţeles după" merită scrisă.

Aritmetica lui `refit_on` se închide până la ultimul ban: 20 de ghiulele × 2 = 40
în plus, 600 → **640** cheltuiţi, 1200 − 640 = **560** rămaşi, iar 20 de ghiulele
la 8 pe reprize sunt 2,5 reprize a câte 0,5 s = 1,5 s în plus, 20,0 → **21,5**.
Fiecare cifră măsurată se potriveşte cu cea calculată.

Şi `crew_fight` nu şi-a mişcat nici avariile, nici pierderile: a unsprezecea
salvă pleca pe la 348 s dintr-o rulare care se termină la 360, deci ghiulelele
ei n-apucau oricum să cadă.

**Task Completed.**

---

## 18.09.2026 — dara ca panglica, nava pe linia ei de plutire, tunurile in mana ta

**Task Started.** Owner, pe rand: „o sa vreau sa punem trails in spatele
ghiulelelor, acum nu se vede foarte bine unde se duc"; apoi „vreau ca darele sa
fie niste linii fumurii care urmeaza ghiulelele si care sunt conice si curbate
dupa traiectorie"; apoi „tunurile sunt putin cam jos, ca inaltime, sau nava
trebuie sa fie mai mare si inalta"; apoi „vreau sa avem un sistem de aiming, cu
unghiuri limitate, ideea este sa fie nevoie sa manevrezi nava ca sa obtii
alinierea, dar tot ar trebui ceva care sa arate directia in care vor merge
ghiulelele" — si, la intrebarea despre cat de strans sa fie arcul, „1, dar vreau
ca pozitia mouse-ului sa dea unghiul, si vreau un buton care fixeaza tunurile la
0 grade [...] ma gandesc sa folosim si mouse wheel pentru elevarea verticala".
Model: Claude Opus 5.

### Emisia nu era moarta. Era de o mie de ori prea slaba.

Dara a fost intai cartele instantiate, si a iesit NEAGRA. Am cheltuit o sesiune
intreaga eliminand cauze: blend mode, model de umbrire, tipul parametrului,
`PerInstanceRandom`, ceata, Lumen, pasa de translucenta, incalzirea shaderelor,
chiar si API-ul de autorare — un material scris prin `MakeMaterialAttributes` era
la fel de negru. Un sweep pe cinci valori de tint, pe ACELASI binar si ACELASI
material, dadea pixeli identici la unitate intre tint 0 si tint 40.

Asta se citeste exact ca o intrare deconectata. Nu era. `DefaultEngine.ini`
porneste `ExtendDefaultLuminanceRange` si blocheaza expunerea la EV100 12,5–16,
deci punctul alb al scenei e cel putin 2^12,5 = **5793 cd/m2**. O emisie de 2,35
ajunge la patru zecimi de miime din alb, si la fel 40. Materialele iluminate se
vad fiindca soarele le da 110.000 lux de reflectat.

**Greseala de rationament, scrisa ca atare:** am tratat „toate valorile dau
acelasi rezultat" ca „intrarea e deconectata", cand insemna „toate valorile sunt
zero DUPA expunere". Testul care ar fi separat cele doua in cinci minute: pune
ceva ILUMINAT pe aceeasi geometrie si vezi daca se vede.

Si fumul de tun e „parcat" de luni de zile din exact acelasi motiv, nediagnosticat
pana acum: `CoreColor` 0,055 si `LitColor` 0,78.

### Dara, a doua oara: o panglica

`AShotTrail` e acum un `UProceduralMeshComponent`. Esantioane la trei metri de
drum, unite intr-o fasie de triunghiuri, deci **curbura e mostenita, nu
calculata**. Conica: ~70 cm la ghiulea, ~520 cm in coada. Un lant pe ghiulea, si
se inchide exact unde cade ghiulea. `trail_stranded` e zavorat pe toata rularea.

### Nava plutea cu un metru mai adanc decat e desenata

`muzzleZ=19` — salva pleca de la 19 CENTIMETRI deasupra apei. Nava e desenata cu
bord liber 2,1 m, dar originea statea la −78,4 cm medie: o sfera de flotabilitate
de raza 320 are nevoie de 375 cm de imersiune, deci centrata pe linia de plutire
nu poate echilibra decat scufundand originea. Coborate cu 80 cm, calibrat pe
patru rulari (legea iese liniara). Si `GGunPortZ` de la 120 — un metru SUB puntea
pe care stau tunurile — la 280.

Originea: −78,4 → **+1,9 cm**. Gura de tun: 19 → **297 cm**. Misca toata
balistica, si in `prize_hull` corsarul devine de doua ori mai eficient.

### Ochirea, si defectul care mi-a aratat ca verificarea mea era goala

Mouse-ul roteste bateria, rotita da inaltarea, `X` fixeaza perpendicular. Arcul
de ±12 grade REFUZA in loc sa taie, iar linia de ochire se face chihlimbarie la
opritor. Trei semne pe apa, pe valuri — nu la zero, fiindca marea e deplasata pe
GPU.

Am declarat ca functia e inerta in suita pe baza unei verificari care compara
**intr-o singura directie**: fiecare diferenta veche apare in rularea noua, 43
din 43. Nu am verificat si invers. Rularea noua avea diferente pe care cea veche
nu le avea — `gunnery.struck: 9 → 8`.

Cauza: deduceam „jucatorul ocheste" din GEOMETRIE (privirea la mai mult de un
grad de travers), ceea ce e adevarat din primul cadru al oricarei rulari fara
mouse. Ochirea manuala se aprindea in toate cele 26 de scenarii.

Semnalul corect e chiar manerul de intrare: un handler de axa ruleaza doar cand
axa s-a miscat. Si cheia `aim_by_hand`, adaugata cu zece minute inainte ca
detector, a prins-o la prima folosire — se vedea in baseline, `gunnery` cu
`aim_by_hand: 1` intr-un rand fara niciun flag de ochire. Instrumentul a
functionat; eu nu m-am uitat la el inainte sa declar victorie.

### Un regex care a incetat tacut sa potriveasca

Cand dara a devenit panglica, linia ei de la iesire a capatat `chains=` intre
`live=` si `discarded=`. Regexul din `ci_measure.py` a ramas pe forma veche si
toate cele patru cifre ale darei au incetat sa fie citite. `trail_off` rula si nu
proba nimic. Nimic nu s-a facut rosu, fiindca `compare()` poate raporta o cifra
ca disparuta doar daca a fost vreodata inregistrata intr-un baseline.

`ci_checks.py` citeste acum `TRAILLOG TOTAL` si `AIMLOG TOTAL` din fixturi
copiate din log-uri reale, plus una deliberat rosie care verifica ca forma VECHE
nu mai e acceptata. Probat prin mutatie: poarta iese rosie, fisierul se
restaureaza octet cu octet.

### Cele trei perechi

- `aim_laid` / `aim_nomarks`: **o singura cheie difera**, `aim_segments` 4 → 0.
  Desenul nu misca fizica.
- `aim_laid` / `aim_stop`: `aim_train` 6,0 → 12,0 si `aim_stop` 0 → 1.
- `gunnery` / `trail_off`: doar cele trei contoare ale darei.

Si niciun scenariu care nu e al ochirii nu are `aim_by_hand` diferit de zero.

**Ramase, stiute si nereparate:** capitanul AI n-a fost atins si trage pe poarta
lui veche de 9 grade; o salva cu un tun costa tot 12 secunde de reincarcare,
fiindca `Reload` e neconditionat, iar proiectul n-are niciun sunet prin care
jucatorul sa afle; `sinking.casualties_max` a cazut la 0 si verificarea aia e
acum vida.

**Task Completed.**

## 19.09.2026 - Reincarcarea pe tun, si o pereche care nu masura nimic

**Task Started.** Prompt: "fa reincarcarea pe tun". Model: Opus 5.

`GunReload[2][4]` in locul celor doua float-uri pe bord. Ticaite neimpartite -
`FullGunCrew` e 48, cu comentariul "sase la un tun, opt tunuri", deci constanta
pretuieste deja bateria intreaga servita simultan; impartind echipajul inca o
data intre tunuri as fi numarat aceiasi oameni de doua ori.

`GetReloadRemaining` e minimul peste tunurile MONTATE, si calificativul ala e tot
ce conteaza: `bGunDown` nu se curata nicaieri in proiect, deci un afet scos isi
tine ceasul pe zero pana la sfarsitul rularii. Un minim peste toate patru ar citi
zero pe veci dupa primul tun pierdut - o poarta deschisa permanent, foc liber,
dintr-un singur tun pierdut.

### Singur, ar fi fost un no-op care se putea proba

O salva trage toate tunurile montate deodata si le stampileaza pe toate cu
aceleasi douasprezece secunde. Patru ceasuri in pas sunt exact cat unul. Ce le
desparte e MAGAZIA, a carei permisiune era un agregat - "o ghiulea pentru fiecare
tun care poarta, altfel nicio salva" - deci o baterie de patru cu trei ghiulele
trage NIMIC, si o spune doar intr-un contor.

### Comentariul care avea dreptate despre pericol si gresea despre iesire

Sustinea ca garda trebuie sa fie inainte de bucla si totul-sau-nimic, fiindca
bucla trage `FMath::FRandRange` de doua ori per tun montat si un tun care refuza
tacut ar sari peste extrageri si ar muta fiecare ghiulea de dupa el.

De protejat sunt extragerile, nu nasterea ghiulelei. Refuzul per tun sta DUPA
extrageri; numarul ramane 2 per tun montat prin constructie. Proba: cele 33 de
scenarii vechi n-au miscat nicio cifra.

### Perechea care nu masura nava pe care o schimbam

`shot_short` / `shot_plenty` au fost scrise ca sa probeze salva partiala, si au
probat nimic. Toate cheile `shot_*` din `ci_measure.py` se citesc dupa nume de pe
`SHOTLOG EnemyShipPawn_N magazine`, iar perechea seteaza `-Shot=`, care e magazia
JUCATORULUI. Amandoua randurile raportau 40/40 de la un inamic pe care niciunul
nu-l atinsese. Doua rulari de motor ca sa masor o coca pe care n-o mutasem.

Cititul dupa nume era instinctul bun - un maxim peste coci ar fi fost mai rau -
dar numele trebuie sa fie coca pe care scenariul o misca. Chei noi: `own_shot_*`,
`own_broadsides`, `own_guns_first`. Poarta are acum o fixtura cu AMBELE linii, cu
cifre diferite, fiindca defectul n-a fost un regex gresit ci un regex corect
indreptat spre nava gresita. Probat prin mutatie: mutat inapoi pe
`EnemyShipPawn_`, poarta iese rosie; restaurat, `cmp` octet cu octet.

Si fixtura panoului de tunuri era ramasa in urma liniei pe care jocul o scrie
acum - linia a capatat `guns_ready_port=`/`guns_ready_stbd=` iar fixtura era pe
forma de patru campuri. Corectata, cu o rosie deliberata langa ea: forma veche
NU mai are voie sa se parseze.

### Ce difera, masurat

`own_guns_first` **3** contra **4**, `own_shot_fired` 3 contra 4,
`own_shot_left` 0 contra 36. Aia e salva partiala.

### Doua campuri de raport pe care le scria un tun care n-a tras

`LaidElevationDeg` si `LastLeadCm` se scriu per tun, sus in bucla, si se citesc o
singura data dupa ea, pentru linia salvei. Era exact cat timp fiecare tun montat
tragea: ultimul tun din bucla era si ultimul care trage.

Cu magazia scurta inceteaza sa fie adevarat. O baterie de patru care plateste
pentru trei refuza tunul din prova DUPA ce si-a socotit propria inaltime si
propriul avans - deci linia raporteaza tunul care a tacut. Si `lead=` hraneste
`lead_max_m`, deci nu e doar cosmetica.

Comentariul de doua randuri mai sus avertizeaza exact despre forma asta - "un
fapt spus de doua ori se desparte, si greseste mereu raportul, tacut" - si campul
a intrat in ea in clipa in care un tun a putut refuza. Amandoua se publica acum
dupa `--Allowed`, adica dupa ce tunul ala si-a platit ghiuleaua.

Un efect secundar, si e o corectie: pe calea ochirii cu mana `LastLeadCm` nu se
scria deloc, deci `lead=` raporta valoarea ramasa de la ultima salva data de
solver. Acum e 0, fiindca tunurile laite cu mana nu iau avans. Daca `lead_max_m`
se misca in scenariile de ochire, asta e motivul.

**Ramas, stiut si nereparat:** ceasurile nu se pot desincroniza azi decat printr-o
magazie prea scurta, iar dupa salva partiala magazia e goala - deci castigul
reincarcarii pe tun se vede abia dupa o reaprovizionare. Structura e acolo si e
probata; fereastra in care se simte e ingusta. Capitanul AI trage in continuare
pe poarta lui veche de 9 grade.

**Task Completed.**

## 19.09.2026 - Fumul, numarat; si o recenzie adversariala peste felia de tir

**Task Started.** Prompt: "Continua". Model: Opus 5.

### Fumul n-avea NICIUN numar

107 chei in linia de baza si niciuna despre fum. Dara are cinci, siajul are
sapte, iar fumul - livrat cu doua commit-uri inainte - n-avea nimic. `SMOKELOG`
exista, dar tipareste un diagnostic o singura data la t=1,0 si `ci_measure.py` nu
citea nimic din el. Daca puf-urile ar fi incetat sa apara, toate scenariile ar fi
ramas verzi.

Patru numere, dupa modelul darei: `smoke_spawned`, `smoke_live_end`,
`smoke_culled`, `smoke_stranded`. Perechea `gunnery` / `smoke_off` difera intr-un
singur flag si intr-o singura familie de cifre: 16 puf-uri contra 0.

**`stranded` e cel care nu trebuie sa se miste niciodata**, si nu e un zero pe
care nimeni nu-l poate clinti: sta imediat dupa garda care omoara un puf trecut
de varsta lui. Probat prin mutatie - cu garda slabita la `LifeSeconds * 10`,
`stranded=22140` si `live` 0 -> 16; restaurat octet cu octet, recompilat
(`[1/4] Compile GunSmoke.cpp`), amandoua inapoi la zero.

Si o capcana ocolita pe drum: PowerShell n-are supraincarcare cu trei argumente
pentru `String.Replace`, deci scriptul care aplica mutatia a aruncat si a tiparit
"mutated" oricum. `cmp` cu copia neatinsa a spus adevarul: fisierul era
NESCHIMBAT. Un control negativ isi verifica propria stricaciune.

### Recenzia: 25 de constatari, 5 verificate adversarial

Cinci recenzori pe cinci dimensiuni, fiecare constatare grava pusa in fata a doi
scepticti cu lentile diferite. **Trei confirmate, una disputata, una respinsa**,
si douazeci lasate neverificate si raportate ca atare.

**Cea grava era a mea, din felia de ieri.** Panoul a ramas pe regula veche a
magaziei - `shot < cate tunuri ai` - cand regula navei s-a mutat pe
`min(Shot, GunsReady) <= 0`. Cateva ore, panoul a strigat MAGAZINE DRY cu rosu,
in aceeasi stiva cu SHE IS GOING DOWN, la un capitan a carui urmatoare comanda
trimitea trei ghiulele. Exact starea pe care felia fusese construita s-o faca
jucabila. **O avertizare care se aprinde cand lucrul MERGE e mai rea decat
niciuna.** Cele doua conditii fusesera scrise ca sa fie aceeasi propozitie; am
mutat una.

**Si controlul perechii era un duplicat.** `shot_plenty` insemna `-Shot=40`, iar
40 E magazia implicita: rand identic bit cu bit cu `gunnery`, 107 chei, nicio
diferenta. O rulare de motor care masura ceva deja in linia de baza. Proiectul
platise deja fix greseala asta o data, cu `magazine_enough` contra
`convoy_weather`, si comentariul care o consemneaza e la treizeci de randuri mai
sus in acelasi fisier. Perechea e acum `shot_short` contra `gunnery`.

Restul, reparate in acelasi commit:

- **O ghiulea care nu s-a nascut cheltuia o permisiune.** `--Allowed` si
  publicarea raportului stateau INAINTE de verificarea `SpawnActor`, deci un tun
  al carui ghiulea a esuat lua ratia unui tun incarcat de mai tarziu. Aceeasi
  reparatie ca cea de ieri, aplicata cu un pas mai jos.
- **`guns_ready_port` era potrivit de regex si aruncat.** Grupul 3, niciodata
  scris in dictionar: o regresie care atingea doar tunurile de la babord n-ar fi
  miscat nimic.
- **Fixtura magaziei nu deosebea doua campuri.** Scria `shot=0/3 fired=3`, deci
  `own_shot_max` si `own_shot_fired` erau amandoua 3 si grupurile se puteau
  inversa nevazut; `own_shot_left` nu era verificat de nimic. Acum 2/7 fired=5.
- **`own_guns_first` nu era prins de PRIMA salva.** Jucatoarea trage exact o
  data in fiecare scenariu, deci nimic nu deosebea `[0]` de `[-1]` sau de un
  maxim. Fixtura cu doua salve, 3 apoi 1.
- **O nava care nu putea trage tinea toti oamenii la tunuri.** `bGunsIdle`
  intreba doar unde e si ce vrea sa faca, deci un raider cu magazia goala si
  greementul ciuruit, aflat in raza, tinea saizeci de oameni servind tunuri care
  n-aveau ce servi - pana la capatul actiunii, fiindca nimic din conditia aia nu
  mai putea deveni adevarat. Greementul nu i se mai repara niciodata.
- **Noua propozitii din documente** care incetasera sa fie adevarate, printre
  care doua liste de "ce ramane de construit" cu bullet-uri rupte si amestecate
  intre ele, si un `-Shot=N` descris ca "fara flag: fara fund" la trei zile dupa
  ce owner-ul decisese contrariul.
- **OWNER_VERIFY 35 cerea ceva imposibil.** Scrisesem ca cele trei pipuri "se
  reaprind pe rand". Nu pot: au tras in aceeasi salva, deci au acelasi ceas.
  Un criteriu de acceptare pe care codul nu-l poate indeplini l-ar fi pus pe
  owner sa caute un defect inexistent.
- **Comentariul care spunea ca fumul e STINS implicit** era fals de doua
  commit-uri; header-ul are `bGunSmoke = true`.

### Zero miscari nu proba nimic despre reparatia AI-ului

Suita n-a miscat NICIO cifra la regula mainilor, si asta nu insemna ca reparatia
e inofensiva - insemna ca n-o atingea nimeni. `crew_repair` termina cu 16
ghiulele la bord, deci nu ramane niciodata uscata; `crew_fight` ramane uscata dar
are reparatiile stinse; iar perechea magaziei lasa greementul raider-ului intact
la 1,00. **Niciun scenariu nu punea o nava AVARIATA si USCATA in raza cu
reparatiile pornite**, adica exact intrebarea pe care regula o decide.

`dry_repairs` o pune: `crew_repair` plus `-EnemyShot=8`. Probat prin mutatie,
scotand `|| bCannotFire`:

| | cu reparatia | fara |
|---|---|---|
| `repaired_max` | **0,90** | 0,748 |
| `enemy_rig_quit` | **0,85** | 0,77 |
| `gun_crew_min` | **0,62** | 1,00 |

`gun_crew_min` e cel limpede: cu reparatia ia oameni de la tunuri, fara ea ii
tine pe toti acolo. Restaurat octet cu octet, recompilat, cifrele revin.

**Respinsa, si merita spus:** constatarea ca un bord cu toate afeturile scoase ar
raporta "gata" si ar tine AI-ul angajat pe o baterie moarta. Amandoi scepticii au
demontat-o.

**Neverificate, raportate ca atare** (20): printre ele, pipurile care nu tin cont
de magazie si deci promit mai multe tunuri decat vor pleca, cele doua stari
palide la 0,30 si 0,12 alpha care s-ar putea confunda, si dreptunghiul de fundal
al panoului GUNS, croit pentru doua randuri cand panoul deseneaza pana la sapte.
Primele doua sunt judecati vizuale si au plecat la OWNER_VERIFY.

**Task Completed.**

## 19.09.2026 - Focul de la gura tunului, si teava din care nu iesea

**Task Started.** Prompt: "Continua". Model: Opus 5.

Owner-ul ceruse pe 16.09, in aceeasi fraza cu fumul: "un mic fum care se
disipeaza mai greu si imediat la tragere un foc scurt". Fumul a fost deparcat cu
doua commit-uri in urma; asta e cealalta jumatate.

**O zecime de secunda.** Praful real e stins in 20-40 ms, adica unul-doua cadre
la 60 fps - sub pragul la care un foc se deosebeste de un cadru pierdut. Sase
cadre e un numar de LIZIBILITATE, nu unul fizic, si l-am ales eu: e in
OWNER_VERIFY 36 ca atare.

Trei cartonase pe axa tevii, micsorandu-se, toate cu fata la camera: un CON din
orice unghi fara ca vreunul sa fie orientat.

**60 000 cd/m², de zece ori punctul alb.** Materialul e scris in unitati fizice
din prima, fiindca lectia era deja platita: cu EV100 ingradit la 12,5 albul e la
5793 cd/m², si un emisiv pe langa 1 iese negru si arata ca o intrare moarta.

**Translucid, nu aditiv, si asta a fost gratis.** Comentariul din
`gunsmoke_material.py` consemneaza ca in proiectul asta fiecare build aditiv n-a
randat nimic dupa ce s-a reparat steagul de ISM. Argumentul pentru aditiv e mai
bun si motorul nu e de acord cu el; ora aia n-a mai fost platita a doua oara.

### Steag separat, si de aceea se poate masura

`-ShipFlash=` nu calareste pe `-ShipSmoke=`: un steag care misca doua lucruri le
masoara suma. Cu doua, `gunnery` contra `flash_off` difera in **exact o cheie**,
`flash_spawned` 16 -> 0, iar `smoke_off` lasa focul la 16 si duce fumul la 0.
Decoratia nu atinge nimic din simulare, si asta e o masuratoare, nu o presupunere.

`flash_stranded` probat prin mutatie: garda de moarte slabita la
`LifeSeconds * 40` da 3760; restaurat octet cu octet, recompilat, zero.

### Si defectul pe care l-a scos la iveala

Captura de dupa arata fumul iesind la nivelul puntii si tevile jos pe bordaj.
Socotit din `Scripts/ship.py`: tevile stateau la **37-49 cm** deasupra liniei de
plutire - practic PE ea - iar tunurile trag de la `GGunPortZ = 280`. Fumul si
focul ieseau cu **2,35 m deasupra tevilor din care ar fi trebuit sa iasa**.

Comentariul de langa `GGunPortsX` spunea "luate din aceleasi pozitii la care au
fost modelate in Blender". Adevarat pentru X - -600/-240/+120/+480 cm sunt fix
-6,0/-2,4/+1,2/+4,8 m - si fals pentru Z, care nu fusese niciodata acelasi numar.
Gaura a fost 77 cm cat `GGunPortZ` a fost 120 si n-a vazut-o nimeni; ridicarea la
280 de ieri a largit-o la 235, iar focul a facut-o evidenta.

Reparat in Blender, mesh regenerat si reimportat de doua ori (comandletul moare
dupa ce scrie asset-ul), materialele reasignate. Numarul e scris o singura data
si il numeste pe celalalt: `GUNPORT_Z = 2.80  # ShipPawn.cpp GGunPortZ = 280 cm`.

**Suita: 37 de scenarii, zero cifre miscate, zero disparute, 109 chei noi.**
Schimbarea de mesh chiar e vizuala - dar asta era o intrebare, nu o presupunere,
iar jumatatea Y a gabaritului a crescut de la 527 la 568 cm fiindca tevile ies
acum de la copastie, unde coca e mai lata. Ghiulelele se nasc la 640, deci tot in
afara lor.

**Ramas, stiut:** nu exista NICIUN sunet in proiect, deci focul e singurul semn
ca a plecat o salva partiala. Si `smoke_culled` e zero in toate cele 37 -
plafonul de 48 de puf-uri nu musca nicaieri azi.

**Task Completed.**

