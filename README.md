# PirateSeas

Prototip Unreal Engine 5.7: navighezi o navă de pirați cu vele pe ocean
deschis și te lupți cu o navă inamică condusă de AI. Plutire fizică, vânt,
tunuri cu balistică reală, integritate de cocă. Totul în C++, nimic în noduri
Blueprint.

Tot ce e aici a fost generat prin script, fără să deschid editorul: nava e
modelată procedural în Blender, iar proiectul Unreal e construit prin API-ul
Python al engine-ului.

## Cum îl joci, fără editor

```
Packaged\Windows\PirateSeas.exe -windowed
```

Un build de sine stătător, care nu are nevoie de Unreal instalat. Se face cu:

```
RunUAT.bat BuildCookRun -project=<abs>\PirateSeas.uproject -noP4 -platform=Win64
  -clientconfig=Development -cook -build -stage -pak -archive
  -archivedirectory=<abs>\Packaged
```

`Development` (nu `Shipping`) fiindcă păstrează logul şi flag-urile de linie de
comandă — toată verificarea proiectului trece prin ele, şi exact aceleaşi
flag-uri merg pe executabil ca pe editor: `-ShipQuitAfter=`, `-Convoy=`,
`-Port=`, tot tabelul de mai jos.

Logul lui nu e lângă cel al editorului, ci lângă jocul
împachetat, la
`Packaged\Windows\PirateSeas\Saved\Logs\PirateSeas.log`. (Aici a scris
`%LOCALAPPDATA%\PirateSeas\Saved\Logs\` până pe 19.09, şi era
greşit: directorul ăla nu există deloc.)

**Pentru prima împachetare a trebuit reparat ce editorul tolera de doi ani.**
Cook-ul numără erorile şi refuză să producă un build; editorul doar le derula pe
ecran. Erau două feluri, amândouă în fiecare rulare de până acum:

- şapte `FBodyInstance::GetSimplePhysicalMaterial: GEngine not initialized`,
  fiindcă nişte constructori chemau `SetSimulatePhysics` / `SetMassOverrideInKg`
  / `SetCenterOfMass` / `SetUseCCD` — apeluri care recalculează masa şi cer
  materialul fizic, în timp ce se construieşte obiectul implicit al clasei,
  înainte să existe motorul. Scrise acum direct pe `BodyInstance`, ceea ce pune
  exact aceleaşi câmpuri. **Măsurat: nu mişcă niciun număr din suită.**
- lipsea profilul de coliziune al apei. Plugin-ul Water şi-l adaugă singur când
  îl porneşti din interfaţa editorului; proiectul ăsta a fost făcut integral
  prin script şi n-a deschis niciodată interfaţa aia, deci apa a mers de la
  început pe un comportament de rezervă. Adăugat în `DefaultEngine.ini` —
  **şi ASTA a mişcat 24 de numere**, izolat printr-o rulare separată.

## Cum îl deschizi

Dublu-click pe `PirateSeas.uproject`. La prima deschidere Unreal compilează
shaderele pentru plugin-ul Water, ceea ce durează câteva minute. Nivelul de
start este deja setat, deci se deschide direct pe ocean.

Apoi apeși **Play**.

## Comenzi

| Tastă | Acțiune |
|---|---|
| W / S | întinzi / strângi pânza |
| A / D | cârma la tribord / babord |
| Săgeți stânga/dreapta | cârma |
| Q | salvă la babord |
| E | salvă la tribord |
| Shift stânga (ținut) | ochești în greement, nu în cocă |
| R | muți un sfert din oameni la reparații, apoi jumătate, apoi toți înapoi la tunuri |
| Mouse | roteşti camera **şi tunurile**; bateria se schimbă singură când treci prova sau pupa |
| Rotiţa | înălţarea ţevilor, 0,2° pe cârtiţă, între −3° şi +10° |
| X | fixezi tunurile perpendicular pe navă şi ignori mouse-ul (comutator) |

**Panoul GUNS arata CARE tunuri iti mai sunt**, nu cate, si in TREI stari.
Patru pipuri pe bord, fiecare despre afetul lui: daca ti-au fost scoase tunul din
pupa si cel din prova, se aprind cele doua din mijloc. Aprins = incarcat, va
trage la urmatoarea comanda; sters pe jumatate = se serveste; aproape invizibil =
afet scos, si ala nu mai revine. Pana pe 19.09 pipurile se desenau dintr-o
NUMARATOARE si se aprindeau de la stanga, deci in exemplul de mai sus ti-ar fi
aratat aprinse exact tunurile pe care nu le aveai.

Capcana din spate merita stiuta: `-ShipGunsDown=N` scotea tunurile 0..N-1, adica
un PREFIX - si contra unui prefix desenul pe numaratoare nimereste din intamplare.
Harnasamentul putea produce doar cazul in care defectul e invizibil. De aceea
exista acum `-ShipGunsDownMask=`, si scenariul `guns_split` cu masca 9 (1001).

**Tunurile se opresc la 12 grade de travers.** Cand mouse-ul cere mai mult, ele
raman la limita si linia de ochire se face chihlimbarie: de acolo incolo doar
carma le mai duce. Vezi „Cum ochesti".

**Nu ai accelerație.** W nu împinge nava, ci întinde pânza. Viteza iese din trei
lucruri: câtă pânză e sus, cât de tare bate vântul și sub ce unghi îl prinzi.

Cârma nu face nimic dacă nava stă pe loc. Autoritatea ei crește cu viteza,
fiindcă are nevoie de apă care curge pe lângă ea.

## Tunurile

Patru tunuri pe fiecare bord. Tragi cu Q la babord și cu E la tribord, iar
**fiecare TUN** se reîncarcă separat, în 12 secunde - nu fiecare bord. Un tun care
n-a tras nu așteaptă după cei care au tras, iar bara de reîncărcare arată cât mai
are cel mai apropiat tun care încă stă în picioare, nu bordul.

Asta contează numai când magazia e prea scurtă ca să plătească toată bateria:
altfel tunurile pleacă toate odată și cele patru ceasuri merg în pas.

Ghiulelele sunt corpuri fizice reale, cu masă de 15 kg și gravitație. Traiectoria
nu e scriptată, iese din simulare.

Tunurile **ochesc**. Q și E aleg singure cea mai apropiată navă de pe bordul
respectiv, echipajele reglează ridicarea țevii pentru distanța ei și rotesc
tunul spre ea cu până la 12°, cât permitea un afet. Fiecare gură de foc are
împrăștiere proprie, mai mare în rotire decât în ridicare, deci o salvă
încadrează ținta în loc să lovească toate în același punct. Măsurat într-o
luptă de 300 s: 14 salve, 28 de lovituri, ratările scurte sau lungi, nu
laterale.

Nava primește recul: fiecare tun împinge coca în sens opus, aplicat chiar în
dreptul gurii de foc, deci o salvă o și înclină puțin.

## Focul de la gura tunului

**Un foc scurt, de o zecime de secunda.** Praful de pusca real e stins in
douazeci-patruzeci de milisecunde, adica unul-doua cadre la 60 fps - sub pragul
la care ochiul deosebeste un foc de un cadru pierdut. O zecime inseamna sase
cadre: destul de scurt cat sa citeasca a foc, destul de lung cat sa fie vazut. E
un numar de LIZIBILITATE, nu unul fizic, si e primul care se schimba daca
owner-ului i se pare ca arata a lampa.

Trei cartonase insirate pe axa tevii, micsorandu-se de la 130 cm la o treime, si
fiecare cu fata la camera. Asta da un CON din orice unghi fara ca vreunul sa fie
orientat - trei discuri de aceeasi marime ar fi dat un bulgare.

**Miezul e la 60 000 cd/m², de zece ori punctul alb al scenei.** EV100 e ingradit
intre 12,5 si 16 cu `ExtendDefaultLuminanceRange`, deci albul e la 2^12,5 = 5793
cd/m²; un emisiv scris pe langa 1, adica valoarea din orice tutorial, iese NEGRU
si arata exact ca o intrare moarta, nu ca una slaba. Proiectul a platit o zi pe
lectia asta la fumul de tun.

Translucid, nu aditiv - si asta nu e o preferinta. Scriptul fumului consemneaza
masuratoarea: dupa ce s-a reparat steagul de folosire pe ISM, **fiecare build
aditiv din proiectul asta n-a randat NIMIC**, pe cand fiecare build translucid
si-a randat cartonasele. Aditivul e argumentul mai bun pentru un foc, si motorul
nu e de acord cu el.

`-ShipFlash=0` il stinge, si e un steag SEPARAT de `-ShipSmoke=`. Un steag care
misca doua lucruri le masoara suma: cu doua steaguri, `gunnery` contra
`flash_off` difera in exact o cheie (`flash_spawned` 16 -> 0) si `smoke_off`
lasa focul la 16 in timp ce duce fumul la 0.

### Si teava din care iese

Construind focul s-a vazut ca **tevile modelate stateau la 37-49 cm deasupra
liniei de plutire, iar tunurile trag de la 280**: fumul si focul ieseau cu 2,35 m
deasupra tevilor din care ar fi trebuit sa iasa. Comentariul de langa
`GGunPortsX` zicea "luate din aceleasi pozitii la care au fost modelate in
Blender" - adevarat numai pentru X.

Gaura era de 77 cm cat `GGunPortZ` a fost 120 si n-a vazut-o nimeni; ridicarea la
280 pe 18.09, ca gurile sa iasa deasupra marii, a largit-o la 235. Tevile sunt
acum la gurile de tun, sub copastie, iar numarul e scris O SINGURA data si il
numeste pe celalalt: `GUNPORT_Z = 2.80  # ShipPawn.cpp GGunPortZ = 280 cm`.

Mesh-ul regenerat n-a miscat nicio cifra din suita - schimbarea e vizuala, si
asta s-a verificat, nu s-a presupus.

## Dara ghiulelei

Fiecare ghiulea trage dupa ea o linie fumurie, ca sa vezi unde se duce. Ceruta de
owner in exact cuvintele astea - nu se vedea unde se duc - si cizelata a doua zi
la ce e acum: "linii fumurii care urmeaza ghiulelele si care sunt conice si
curbate dupa traiectorie". E **lizibilitate**, nu podoaba: nu-ti poti corecta
tirul daca nu vezi unde a cazut cel dinainte.

O PANGLICA, nu un sir de puncte. Eantioanele se iau la trei metri de drum si se
unesc intr-o fasie de triunghiuri, deci **curbura nu e calculata, e mostenita** -
panglica urmeaza traiectoria fiindca e facuta din ea. Conica: ~70 cm la ghiulea,
unde fumul abia a plecat, si ~520 cm in coada, unde patru secunde de aer l-au
tras in parti. Varful conului iti spune unde e lovitura ACUM.

Un lant pe fiecare ghiulea, nu unul pe lume: patru ghiulele dintr-o salva deseneaza
patru arce, iar un singur cursor impartit intre ele ar desena o linie punctata cu
trei sferturi lipsa. Si se inchide EXACT unde cade ghiulea, nu cu trei metri
inainte - altfel ar rata singura intrebare pentru care exista.

**Pe TOATE ghiulelele, si pe ale inamicului**, prin decizia owner-ului. Asta
schimba jocul, nu doar imaginea: vezi salva care vine si poti pune carma.

`-ShotTrails=0` le stinge. Perechea `gunnery` / `trail_off` difera intr-un singur
flag, si **jumatatea care conteaza e cea care NU trebuie sa difere**. Contoarele:
`trail_laid`, `trail_chains`, `trail_discarded` si `trail_stranded` - ultimul nu
are voie sa se miste niciodata, si e zavorat pe toata rularea, nu esantionat pe
cadru.

**Prima incercare a fost pe cartele instantiate si a iesit NEAGRA.** Nu materialul
era de vina: scena ruleaza in unitati fizice si punctul ei alb e 5793 cd/m2, deci
o emisie de 2,35 - sau de 40 - ajunge la patru zecimi de miime din alb. Un sweep
pe cinci valori dadea pixeli identici, ceea ce se citeste exact ca o intrare
moarta si nu era. Vezi `Source/PirateSeas/ShotTrail.h` pentru toata socoteala.

## Cum ochesti
## Cum ochesti

Tunurile urmeaza MOUSE-UL, nu doar camera. Directia in care te uiti devine
directia bateriei, iar bordul se schimba singur cand privirea trece prova sau
pupa. Rotita urca si coboara teava. `X` fixeaza tunurile perpendicular pe nava
si ignora mouse-ul pana il apesi din nou - e comutator, nu apasare tinuta,
fiindca ideea e sa-ti iei mana de pe tunuri si sa te intorci la carma, iar o
tasta pe care trebuie s-o tii apasata nu-ti da inapoi nicio mana.

**Arcul e de 12 grade in fiecare parte, si NU e o sugestie.** Cand mouse-ul cere
mai mult, tunurile raman la limita si linia de ochire se face chihlimbarie. Nu
scrie nicaieri "intoarce nava": se vede ca tevile nu mai merg, si asta e singurul
fel in care o regula chiar se invata.

Cat de strans e asta, in cifre. Cele patru guri de tun stau la 10,8 m una de
alta, deci vad tinta sub unghiuri diferite - dar cat de diferite depinde tare de
distanta:

| distanta | unghiurile celor patru tunuri | dispersie |
|---|---|---|
| 50 m | +7,8 +3,2 -1,6 -6,3 | **14,1 grade** |
| 100 m | +3,7 +1,5 -0,7 -2,9 | 6,6 |
| 400 m | +0,9 +0,3 -0,2 -0,7 | 1,6 |

La 50 m dispersia e mai mare decat tot arcul, deci la distanta de abordaj e
fizic imposibil ca toate patru sa poarte daca nu esti aproape paralel. La 400 m
ori poarta toate, ori niciunul. Alinierea e chinuitoare de aproape si curata de
departe, ceea ce e si istoric drept si exact tensiunea care face manevra sa
conteze.

**Ce vezi pe apa**, trei semne, fiecare raspunzand la altceva: doua linii palide
la opritoarele afeturilor (cat loc mai au tunurile), o linie clara acolo unde
arata tevile (pe care o conduci cu mouse-ul si care se OPRESTE cand mouse-ul nu
se opreste), si o bara transversala la distanta pe care o da inaltarea curenta -
aia e cadranul rotitei. Fara ea ai invarti un buton fara nicio citire pe el.

**Si bara nu minte, masurat.** Pe douazeci de lovituri la fiecare inaltare, fata
de unde cad ghiulelele de fapt:

| inaltare | bara zice | cad la | imprastierea salvei | eroare |
|---|---|---|---|---|
| 2 grade | 217 m | 226 m | +/-11,9 m | +9 m |
| 6 grade | 487 m | 489 m | +/-13 m | +2 m |

Eroarea e sub imprastierea proprie a salvei, adica sub ce poate sti tunul insusi.

Prima versiune a barei MINTEA, si urat: la doua grade arata 153 m acolo unde
ghiulelele cadeau la 228 - patruzeci si noua la suta, si sase abateri standard.
Folosea formula balistica pentru tragere de la nivelul SOLULUI, iar tunurile
statusera dintotdeauna aproape pe linia de plutire... pana cand au fost ridicate
la 2,97 m, cu o zi inainte. Cadranul ramasese calibrat pentru o nava care nu mai
exista. Formula tine acum cont de inaltimea reala a gurii deasupra marii LOCALE,
deci si de val.

Ce ramane, spus pe fata: reziduul isi schimba semnul pe la sapte grade, ceea ce
inseamna ca nu mai e geometrie ci modelul de rezistenta a aerului - `RangeBias` e
o constanta, iar rezistenta depinde de timpul de zbor, deci o constanta nu poate
fi corecta la toate inaltarile. Sub un procent la distantele la care se lupta.

Semnele stau pe VALURI, nu la zero: marea e deplasata pe GPU in `M_Sea`, si un
semn plat la zero ar sta jumatate din timp ingropat.

**Ochirea manuala e o cale separata, si asta e deliberat.** Calea prin care
tinteste capitanul AI si harnasamentul `-ShipFireTest` raman neatinse, deci
niciunul din cele 26 de scenarii care existau inainte nu se misca din cauza ei -
probat, nu presupus: toate cele 43 de diferente vazute inainte de a o adauga apar
identic si dupa. (Celelalte trei randuri din cele 29 sunt ale ochirii insesi, si
evident ca ele nu existau.) Un sistem de ochire care ar fi re-inregistrat baseline-ul in ziua in
care a fost adaugat si-ar fi ascuns propriul efect intre o suta de alte cifre
schimbate.

Si inca una, fiindca e o decizie si nu o omisiune: cand ochesti de mana, tunurile
NU mai primesc tinta. `OnFirePort` nu mai cheama `FindTargetOnSide`, altfel
rezolvitorul ti-ar re-ochi tevile pe furis. Cine isi aliniaza singur lovitura are
dreptul sa rateze.

`-LayTrain=` si `-LayElev=` fixeaza ochirea fara mouse, ca sa se poata masura si
fotografia deloc. Perechea `aim_laid` / `aim_stop` arata ca opritorul musca -
`+6,0` intr-un rand si `+12,0` in celalalt, cu `aim_stop` 0 si 1 - iar
`aim_nomarks` stinge DOAR desenul, si toate celelalte cifre din rand trebuie sa
ramana identice pana la cifra.

## Balistica
## Balistica

**Gravitaţia, da.** Ghiuleaua e un corp rigid care simulează, cu
`SetEnableGravity(true)`, iar solverul de elevaţie are `g = 980 cm/s²` scris
explicit. Verificat prin măsurătoare, nu citit din flag: la elevaţie 4,80° şi
gura tunului la 1,24 m, ballistica prezice 2,585 s de zbor, iar logul dă
2,52–2,58 s pe cele patru ghiulele ale salvei. Viteza orizontală iese 145 m/s şi
la 148 m şi la 365 m, deci **nu există rezistenţă a aerului** — un lucru care se
poate adăuga, dar acum nu e acolo.

**Viteza şi direcţia navei, de pe 13.09 — da.** Până atunci, nu: ghiuleaua
pleca cu `Aim * MuzzleSpeed` şi atât, iar tunurile erau ochite pe unde se afla
ţinta **atunci**, nu pe unde avea să fie. La 145 m/s şi 2,5 secunde de zbor la
bătaie lungă, o navă care face 6,5 m/s se mută şaisprezece metri — mai mult de
jumătate din lungimea ei, şi de-a curmezişul liniei de tir, fiindcă tunurile
trag pe travers.

Cele două merg împreună şi asta e partea măsurabilă. Anticiparea se calculează
pe viteza **relativă**: în sistemul navei care trage, ghiuleaua pleacă cu viteza
de la gură iar ţinta derivă cu (ţintă − propriu), deci aia e singura viteză
pentru care trebuie ochit înainte. Două nave care merg alături cu aceeaşi viteză
nu cer nicio anticipare, iar forma relativă spune asta fără niciun caz special.

Măsurat pe patru seminţe, 150 s, două nave inamice:

| variantă | salve | lovituri | lovituri/salvă |
|---|---|---|---|
| vechi (nici una) | 28 | 33 | 1,18 |
| doar moştenirea vitezei | 28 | **5** | 0,18 |
| doar anticiparea | 28 | 16 | 0,57 |
| **ambele (implicit)** | 28 | 32 | **1,14** |

Fiecare jumătate singură e dezastruoasă, şi asta e dovada că perechea face o
treabă reală în loc să nu facă nimic: moştenirea fără anticipare mută ghiuleaua
lateral cu tot atât cât se mută nava, iar anticiparea fără moştenire
supra-corectează cu exact aceeaşi cantitate. Împreună dau aceeaşi precizie ca
modelul vechi — **nu e un câştig de precizie, e o corectare de fizică**, şi se
plăteşte singură în situaţiile pe care modelul vechi nu le putea reprezenta
deloc.

`-ShipInheritVel=0` şi `-ShipLead=0` readuc exact comportamentul vechi, ceea ce
e tot ce face schimbarea măsurabilă în loc de doar argumentată.

**Despre `RangeBias`.** Tunurile trag cu 4% lung, şi bănuiala a fost că numărul
compensa tocmai anticiparea care lipsea. Baleiat de la 0,80 la 1,40 cu fizica
pornită: media loviturilor se mută într-adevăr, de la −140 m la −105 m, deci
parametrul funcţionează. Dar între 0,98 şi 1,06 diferenţa e **o singură
lovitură din treizeci şi trei**, pe patru seminţe. Aia e împrăştiere, nu semnal,
şi nu se calibrează nimic pe ea. Rămâne 1,04.

## Urmele loviturilor

**O lovitura in cocca lasa un semn care ramane.** Pana pe 25.09 se vedea in
cifre, nu pe lemn: o bara HULL scazand si un manunchi de aschii care se stingea
intr-o secunda. Acum fiecare ghiulea intrata in lemn lasa un disc intunecat de
60 cm la punctul si cu inclinarea pe care le-a raportat coliziunea cocii, si
discul calatoreste cu nava. E lemnul ei intunecat (`MI_DarkWood`), fara niciun
material nou - un negru luminat de soare e ce e o gaura in stejar la trei sute
de metri.

Asta e jumatatea care RAMANE din „feedback la contact" - aschiile sunt cea care
trece - si a devenit posibila abia cand ghiuleaua a inceput sa se opreasca pe
lemn: pe cutia de coliziune semnul ar fi plutit cu un metru in afara bordajului.

Numarate, nu presupuse: `HOLELOG` per nava la iesire, iar `holes_total` adunat
peste toate cocile trebuie sa fie egal cu `hull_hits` pe fiecare rand din
suita - aceeasi poarta sub care traiesc aschiile. `-ShipHoles=0` le stinge, si
perechea `gunnery` / `holes_off` difera in exact familia asta. Plafon 32 pe
nava, cele mai vechi primele, si taierea e numarata (`holes_culled`) - un zero
pe care niciun scenariu de azi nu-l misca: randul cel mai lovit din suita
numara 15 gauri, peste toate cocile lui.

## Coca pe care o loveste ghiuleaua

**Pana pe 25.09 o ghiulea se oprea pe o CUTIE.** Radacina navei e o cutie de
coliziune de 1550 x 520 x 350 cm - trebuie sa fie, fiindca plutirea actioneaza pe
radacina - si tot pe ea se opreau si ghiulelele. Masurat cu
`tools/probe_hull_hits.py`, pe cele sapte lupte distincte din suita: lemnul era
in medie la **1,7 m** (zona tunurilor) pana la **3,4 m** (coca) in spatele fetei
cutiei, si pana la **7 m** la capete, unde coca se subtiaza si cutia nu. Fiecare
zona de avarie, fiecare manunchi de aschii si fiecare capat de dara era pus
acolo.

**Acum se opreste pe lemn.** `AShipPawn::HullShot` e pielea cocii - lofata din
ACELEASI sectiuni ca nava vizibila (`Scripts/ship.py`, `build_shot_hull`),
inchisa, cu parapet, niciodata desenata - folosita ca „coliziune complexa drept
simpla": coliziunea E triunghiurile ei. Intra pe aceeasi cale ca greementul:
QueryOnly, `ECC_Vehicle`, gasita de sweep-ul ghiulelei, fara niciun canal nou de
configurat. Cutia ramane radacina si IGNORA ghiuleaua - exact ce facea deja o
epava, ca sa nu opreasca niciun tir.

Masurat dupa: **0,00 m** intre punctul raportat si lemn, la fiecare lovitura.

**Doua lucruri care au mers prost pe drum, si merita stiute:**

- Prima versiune a exportat sase felii convexe `UCX_*` pentru importator, care
  le-a aruncat TACUT: asset-ul a venit cu `convex_elems=0`. Jocul ar fi raportat
  „0 lovituri", nu „lipseste coliziunea". Scriptul de import nu poate verifica
  nimic dupa import (commandlet-ul moare acolo), deci verificarea e alt script,
  `Scripts/hull_collision.py`, care seteaza steagul, salveaza si RECITESTE de pe
  disc.
- A doua versiune s-a inchis la copastie, fara parapet. Parapetul e lemn, si
  e exact banda in care sunt taiate gurile de tun; e in piele acum, iar
  scriptul de coliziune refuza o piele al carei varf e sub copastie.
- Tot 37 de lovituri unde cutia avea 41, si cu parapetul: in fiecare lupta
  mutata din suita, exact o lovitura a devenit exact un strop (-1/+1). Cea mai
  probabila explicatie, din recenzie: o ghiulea care cobora sub val in golul de
  1,7-4,7 m dintre fata cutiei si scanduri se oprea pe cutie; acum cade in mare
  inainte sa ajunga la lemn. Marea e acolo; e corect.

Cifra din aceeasi proba care e a owner-ului, nu a mea: **13 din 37 de lovituri
scot un tun din afet, si toate 13 au intrat sub punte** - banda de avarie a
tunurilor e `[0, 240]` de cand gurile erau la 120. `OWNER_VERIFY` 38.

**Si o poarta moarta care a inviat.** Clasificatorul cerea `|Y| >= 380` inainte
sa caute o gura de tun - „pe travers". Pe cutie, orice lovitura in bord avea
|Y| = 520 si testul nu facea nimic. Pe lemn, |Y| e jumatatea de latime a cocii
la statia aia: 371 cm cel mult in fereastra tunului 1, 333 in a tunului 4 -
deci doua tunuri pe bord deveneau de nelovit, iar prima cifra pusa in fata
owner-ului („7") era poarta asta, nu lemnul. O recenzie a prins-o; e scoasa.

## Unde lovești contează

O ghiulea nu mai scade pur și simplu un număr. Contează unde intră.

| Zona | Cum o atingi | Ce costă |
|---|---|---|
| cocă | tir jos, pe bordaj | 60 din 1000; la zero se scufundă |
| greement | tir înalt, în catarge și vele | pânza; cu tot greementul dus, nava e moartă în apă |
| cârmă | doar printr-un tir prin pupă, aproape de axă | cârma; fără ea se rotește de cinci ori mai încet |
| baterie | printr-un sabord, la înălțimea punții tunurilor | un tun; bordul acela trage cu o ghiulea mai puțin |

Măsurat, la vânt bun:

| avarie | efect |
|---|---|
| greement 100% → 60% → 30% → 0 | 6,65 → 5,92 → 4,52 → **0,01 m/s** |
| cârmă 100% → 50% → 0 | 5,45 → 3,25 → **1,17 °/s** |
| 2 tunuri scoase pe bord | salva trage 2 ghiulele în loc de 4 |

**Tirul înalt nu scufundă pe nimeni.** Îi ia mersul. Tirul jos o scufundă, dar o
lasă să fugă până atunci. Asta e alegerea, și e aceeași pe care o făceau
englezii (jos, să spargă coca) și francezii (sus, să taie greementul).

O salvă atinge aproape întotdeauna mai multe zone deodată, și așa trebuie: patru
ghiulele împrăștiate pe 1,6° acoperă la 300 m vreo 17 metri, mai mult decât
jumătate de navă. Ordinul „sus" sau „jos" înclină balanța, nu comută între două
rezultate curate.

Ține **Shift stânga** apăsat când apeși Q sau E și echipajele ochesc în greement.

Inamicul urmează o doctrină pe care o poți prezice: **trage sus până îi rămâi cu
sub jumătate din pânză, apoi trage jos**. Întâi te schilodește, ca să nu poți
alege distanța și nici fugi, apoi te scufundă.

Dacă ai ambele tunuri de pe un bord scoase, salva pe bordul ăla nu mai pornește
deloc și nu-ți consumă nici reîncărcarea.

## Uscatul

Sunt insule pe mare, cu `-Islands=1`. O insulă are o plajă la 110 m de centru,
un deal de 42 m deasupra apei și un banc de nisip care se întinde încă 50 m în
larg de plajă.

**Nava nu atinge niciodată stânca.** Nu e o alegere de gust, e concluzia unei
măsurători: cu uscat care blochează coca, un corp de 60 de tone împins în el e
rezolvat de motorul de fizică singurul fel în care poate fi rezolvată o
pătrundere — în sus, pe fața de deasupra. 302 contacte, normala impactului 1,00,
navele urcate la 25 m și rămase acolo cu 0,00 m/s până la sfârșitul rulării. Sus
n-are niciun pontoon în apă, deci chila, velele și cârma se sting toate în
aceeași clipă și nimic din proiect n-o mai poate mișca vreodată.

Deci eșuezi pe **banc**: apa mică din jurul plajei te împinge înapoi în larg, cu
atât mai tare cu cât stai mai adânc. Trei lucruri de știut despre banc:

- **Împinge doar spre larg.** Nicio valoare a lui nu poate ține o navă care
  pleacă: îți poate lua viteza, altceva nu.
- **Se întărește mult spre mal** — de cincizeci de ori spre ultimul metru, apoi
  plafonat. *Nu* la infinit: e un banc destul de tare, măsurat, nu unul imposibil
  de trecut prin construcție. Diferența contează, și e spusă aici fiindcă prima
  versiune a acestei fraze promitea garanția pe care codul nu o dă.
- **Te măsoară pe toată lungimea, nu într-un punct.** Banca se citește în trei
  puncte — prova, centrul, pupa. Cu un singur punct la mijloc, o navă de 31 m
  tratează un banc de 50 m ca pe unul de 34, iar cei 16 m lipsă sunt exact acolo
  unde e prova: măsurat înainte de asta, ținută în banc cu toate pânzele sus, se
  oprea cu etrava la doi metri și jumătate dincolo de țărm, cu toate citirile în
  verde.
- **Te costă odată, la atingere**, pe cât de repede **intri în banc** — nu pe cât
  de repede mergi. O navă care aleargă de-a lungul coastei și doar atinge marginea
  era taxată pentru toată viteza ei, din care aproape toată era paralelă cu malul.
  O atingere lină e aproape gratis; intratul cu toate pânzele sus te costă un
  sfert din cocă și te poate scufunda acolo unde stai, prin exact aceeași inundare
  ca o ghiulea.

Cum ieși: **strângi pânza.** E singura manetă a unei nave cu vele pătrate și e
și răspunsul corect istoric — n-o împingi mai tare pe uscat.

Măsurat, intrând cu toate pânzele sus, cu 6,56 m/s spre mal: rană de cocă
1000 → 705, adâncimea maximă **la provă** 37 m din 50 disponibili (deci etrava s-a
oprit la 13 m de plajă), viteză la ieșire 0,06 m/s — un arc fără amortizor te-ar
fi aruncat afară mai repede decât ai intrat — și 30 de secunde de la atingere
până e iar liberă. Pescajul nu se schimbă o clipă: `inWater=1` și portanța în jur
de 1,00 pe toată durata.

O insulă așezată peste un punct de apariție e **refuzată**, nu doar semnalată: o
cocă născută în banc e azvârlită afară cu 30 m/s, și un avertisment într-un log
de 2700 de rânduri e un lucru pe care nu-l citește nimeni înainte să creadă
cifrele de sub el.

**Stânca oprește ghiulelele.** Măsurat pe o pereche de rulări identice, cu
aceeași sămânță: fără insulă 8 ghiulele cad în apă, cu insulă 5 cad în apă și
**exact 3 se opresc în stâncă** — aritmetica se închide — iar loviturile în
greement rămân 4 și 4. Uscatul e adăpost, și e un adăpost care se numără în log
(`SHOTLOG land`), nu unul dedus dintr-o scădere a loviturilor.

### Cum schimbă bordul

**Când are viteză, vine în vânt. Când n-are, virează prin pupă.**

Asta a fost, până acum, singura parte a navigației pe care căpitanul o făcea
categoric prost. Regula veche spunea „dacă drumul cel scurt taie vântul, virează
prin pupă" — fără nicio condiție de viteză, deși propriul ei comentariu explica
de ce: *o navă cu vele pătrate trece prin ochiul vântului doar cu viteză
adevărată.* Deci vira prin pupă și la șase noduri, când ar fi putut veni în vânt
în câteva secunde.

Măsurat, bordeind 600 m până la o țintă aflată în vânt: câștiga vreo 40 m pe
fiecare bord și pierdea vreo 110 m la fiecare viraj prin pupă. **În patru sute de
secunde termina cu 285 m mai DEPARTE decât plecase, și nu trăgea niciun foc.**

| | virează mereu prin pupă | vine în vânt când poate |
|---|---|---|
| lupta standard, salve | 3 | **7** |
| aceeași, lovituri | 4 | **13** |
| escadron de două, salve | 5 | **6** |
| bordei de 600 m, salve | **0** | **3** |
| aceeași, distanța la final | 885 m | **380 m** |

**Prețul, spus pe față:** cea mai lungă perioadă prinsă cu prova în vânt crește
de la vreo 6 secunde la vreo 40. Uneori venirea în vânt eșuează și trebuie
plătită. Merită: un viraj prin pupă costă o sută de metri sub vânt de **fiecare
dată**, iar o venire ratată costă patruzeci de secunde **o dată**.

Pragul e 4 m/s, iar treapta e ascuțită fiindcă viteza ei în bordei e 4,89: peste
5 testul nu trece niciodată, sub 4,5 trece mereu. `-AITackAbove=N` îl schimbă, și
o valoare foarte mare readuce exact comportamentul vechi — de asta se poate
măsura în loc să se discute.

### Căpitanul inamic și uscatul

AI-ul **nu ocolește** uscatul: n-are privire înainte, n-are rută, și intră în
banc dacă drumul spre tine trece prin el. Ce face e un singur lucru, și e
lucrul care contează: **odată eșuată, nu se mai mână singură pe uscat.** Strânge
din pânză cât timp încă înaintează spre mal, apoi o pune la loc și cârmește spre
larg pe cel mai bun curs pe care rigul chiar îl poate ține — adică bordeie, dacă
largul e în vânt.

De ce doar atât: măsurat, ce o ținea pe uscat era **pânza**. Comanda „toate
pânzele sus" rula în fiecare cadru fără nicio idee că sub ea e nisip, iar bancul
o împingea afară în timp ce ea se mâna înapoi înăuntru — se vede în log, oscila
între 28 și 38 de metri în banc timp de șase minute.

Măsurat, pe un mal sub vânt cu insulă de 180 m rază:

| | înainte | după |
|---|---|---|
| secunde eșuată (din 400) | 190 | **42** |
| salve trase în toată rularea | **0** (nu ajungea la luptă) | 14 |
| aceleași, cu greementul la 0,45 | 380 s eșuată | **66 s**, 16 salve |
| insulă implicită, pe drum | o atingere, liberă în 14,8 s | o atingere, liberă în **9,6 s** |
| fără insulă în lume | — | log **identic bit cu bit**, `landTicks=0` |

**Prețul, spus pe față:** atinge uscatul mai des decât înainte (de două ori în
prima rulare, de cinci ori cu greementul rupt), fiindcă după ce se dezlipește
ținta e tot dincolo de insulă și ea o ia iar într-acolo. Pierde deci mai multă
cocă pe uscat decât în măsurătoarea veche — dar în măsurătoarea veche nu era în
siguranță, era **blocată**. Privirea înainte, care ar opri atingerile repetate,
e exclusă deliberat din felia asta.

**Poate să ocolească, dar implicit nu o face.** Privirea înainte e construită și
măsurată — `-AILookAhead=90` o pornește — și verdictul măsurătorii e că nu merită
pornită. Pe o coastă de trei insule înjumătățește eșuările și nu costă nimic
(16 salve față de 13, jucătorul ia exact aceeași bătaie). Dar pe cele două
geometrii unde uscatul stă **între ea și țintă** o face inofensivă: pe un mal sub
vânt salvele scad de la 14 la 9 și jucătorul termină cu 760 din cocă în loc de
400, iar pe insula implicită nu-l mai scufundă deloc.

Două geometrii din trei pierd mai multă luptă decât salvează eșuări. Și mai e
ceva ce cifrele nu spun: un inamic care ocolește mereu promontoriul e un inamic
pe care nu-l poți împinge niciodată pe un mal — adică exact lucrul cel mai
interesant pe care uscatul îl poate aduce în jocul ăsta.

Sub vreo cincizeci de secunde de orizont e de-a dreptul dăunătoare: la douăzeci a
eșuat de **opt** ori pe coastă, fiindcă vede uscatul prea târziu și apoi taie
zig-zag.

**Ce tot NU face:** nu se ferește de un mal sub vânt înainte să ajungă pe el, și
**trage în continuare prin insulă**. Ultima e o decizie, nu o
scăpare: testul de „nu trage prin consoartă" se blochează latch-uit, iar pe o
geometrie fixă ca o insulă ar putea amuți un escadron 300 de secunde cu toate
celelalte indicatoare verzi. O ghiulea irosită e mai ieftină decât o luptă
pierdută în tăcere.

**Și o navă care rămâne în urmă se întoarce acum în linie.** Comentariul din
felia escadronului spunea deja că „mai departe de un interval nu mai ține
postul, ci se reîntoarce" — dar reîntoarcerea nu fusese construită, fiindcă
nimic nu arunca o navă destul de departe. Uscatul a aruncat-o: măsurat, o navă
care atinsese bancul termina rularea la 66 km de post, cârmind un curs paralel
cu o linie de care nu mai era nici pe departe. Acum, peste trei intervale,
cârmește spre PUNCTUL de post și pune pânză plină; se întoarce în linie.

## Escadronul

Nu te așteaptă o navă, ci două. `-EnemyCount=N` schimbă numărul, până la opt.

Se formează în **șir**, fiecare ținând locul în urma celei dinainte, la vreo
120 m. Nu e o decorație: tunurile trag pe travers, deci într-o linie de front
consoarta ți-ar sta fix în bătaie. Măsurat cu trei nave în linie de front, una a
pus o ghiulea în alta în primele două secunde. În șir, consoartele stau la 90°
de unde pot trage tunurile. De asta a existat linia de bătaie.

Doar nava din frunte navighează: alege tactica, distanța, bordul. Celelalte îi
țin locul, cu cârma pe cursul celei dinainte și cu **pânza** pe distanță,
fiindcă o navă cu vele pătrate n-are altă manetă. Când cea din frunte se
scufundă, următoarea preia conducerea.

Două reguli care le țin în viață una lângă alta:

- **Nu trag prin consoartă.** Dacă o navă de-a lor e în bătaia tunului, bordul
  acela tace până se eliberează culoarul.
- **Se feresc.** O navă care vine prea aproape de o consoartă pune cârma în
  partea opusă, cu atât mai tare cu cât e mai aproape.

Măsurat cu trei nave, 300 de secunde: **zero ciocniri** (erau șase, una cu impuls
de 8,1e6) și **zero lovituri fratricide** (era una).

Panoul îți spune câte mai sunt pe apă. Victoria se numără pe escadron, nu pe
navă: nu ai învins până nu s-a dus și ultima, iar escadronul următor iese la
mare la opt secunde după aceea.

## Convoiul

Primul motiv să lupţi. `-Convoy=N` pune N negustori pe mare (până la şase), care
ies dintr-un punct (`-ConvoyX= -ConvoyY=`, implicit la 1,9 km spre nord-est) şi
fug pe un drum aşezat la `-ConvoyWindAngle=` grade faţă de vânt spre o radă la
`-ConvoyRangeM=` metri (implicit 1500). Implicit drumul e la 90 de grade, adică
DE-A CURMEZIŞUL vântului, ca „în vântul convoiului" şi „sub vântul lui" să
însemne acelaşi lucru toată partida, nu doar la început. O navă ajunsă la 150 m
de radă e sub tunurile fortului şi a scăpat.

Un negustor nu luptă până la ultima scândură: **coboară pavilionul** când i-a
rămas sub 60% din cocă sau sub 60% din greement. Atunci strânge pânza, pune
cârma la mijloc şi nimeni nu-l mai vânează — dar loviturile tot intră: cine
continuă să tragă într-o navă care a coborât pavilionul o scufundă, şi pierde
exact ce voia.

Convoiul e **LUAT** când ai oprit `-ConvoyNeed=` nave (implicit jumătate,
rotunjit în sus) şi **A TRECUT** când destui au ajuns în radă sau pe fund încât
numărul ăla nu se mai poate atinge. Panoul arată socoteala. Fără prăzi, fără
valoarea mărfii, fără escortă, fără abordaj: astea sunt feliile următoare. Asta
e doar partea care face din **partea vântului pe care stai** decizia — polara
măsurată a proiectului o preţuieşte la vreo 8:1 (500 m sub vânt costă 62 s,
500 m înapoi în vânt costă 481).

Negustorul e aceeaşi cocă, acelaşi greement, acelaşi căpitan AI (care ştie că e
negustor şi navighează în loc să lupte), dar ÎNCĂRCAT: viteza lui maximă e 55%
din a unei nave de luptă — măsurat, 3,6 m/s faţă de 6,5. Nu pânza e butonul: cu
jumătate de pânză tot făcea 5,2 m/s, fiindcă forţa scade cu 1 − v/Vmax şi marea
dă cea mai mare parte înapoi. Linia de plutire e butonul.

`-RaiderSide=weather|lee` şi `-RaiderOffingM=` aşează ESCADRONUL inamic în
vântul convoiului sau sub vântul lui, la atâţia metri de-a lungul vântului.
Raider-ul dintr-o rulare măsurată e o navă inamică obişnuită cu căpitanul
obişnuit, care vânează cea mai apropiată cocă duşmană — iar negustorii îi sunt
duşmani. Nava jucătorului stă unde e. `-ConvoyStrikeTest=N` face primul
negustor să coboare pavilionul la N secunde fără nicio ghiulea, ca drumul de la
pavilion la misiune încheiată să se poată dovedi singur.

### Ce a trebuit reparat la căpitan ca să poată prinde un negustor

Până acum, în raza de angajare, vira să-şi pună tunurile pe ţintă ORICE ar fi
făcut ţinta. Contra unei nave care stă şi luptă e corect; contra uneia care fuge
înseamnă că stă la 490–540 m două sute de secunde fără să tragă un foc — măsurat
cu raider-ul în vântul convoiului, cu 800 m avans. Acum, în afara distanţei de
menţinere şi fără să câştige teren, se duce DREPT spre ea şi pune tunurile când
ajunge. Cu histereză pe DISTANŢĂ, nu pe viteza de apropiere: fugarul deschide
distanţa exact când raider-ul virează să tragă, deci o regulă care relua
urmărirea la 350 m o relua la două cadre după fiecare viraj (293 de rânduri
„running down" într-o rulare, şi o navă care zigzaga în loc să termine vreunul
din drumuri).

Şi îşi păstrează ţinta până iese din luptă. Recăuta la fiecare două secunde —
inofensiv cu o singură cocă duşmană în lume, ruinător cu doi negustori la 150 m
unul de altul: patru ghiulele în greementul unuia, „cel mai apropiat" sare la
celălalt, trei în al lui, şi niciunul destul de rănit ca să coboare pavilionul.

## Linia de bataie

Escadronul inamic navigheaza in sir, si fiecare nava tine pozitia dupa cea din
fata ei - nu dupa lider. Un lant de brate scurte se corecteaza singur, pe cand un
sir intreg tinut pe o singura nava ii pune toata eroarea acumulata in coada.

**Si se STRANGE cand un consort cade din linie.** O nava care si-a coborat
pavilionul nu e scufundata: e pe linia de plutire, in deriva, fara curs. Pana pe
19.09 linia continua sa se alinieze dupa ea, si toata coada o urma oriunde o
ducea marea. Comentariul din cod promitea de la inceput "cea din fata ei care
inca guverneaza"; bucla nu verifica nimic.

Perechea `line_closes` / `line_whole` difera intr-un singur flag,
`-EnemyStrikeTest=10`, si intr-o singura cifra: `line_skips`, adancimea la care a
trebuit sa se uite linia peste cei cazuti. 1 cand unul a coborat pavilionul, 0
cand niciunul, si **tot 1 cu cinci nave in loc de trei** - fiindca e o adancime,
nu un numar de urmaritori.

Cifra a fost gresita de doua ori inainte sa fie buna, si amandoua greselile merita
stiute: intai numara tick-uri (3600 intr-o rulare de treizeci de secunde, adica
rata de cadre purtand un nume tactic), apoi aduna adancimile fiecarui urmaritor
(2 acolo unde o singura nava cazuse). A doua a fost prinsa cu o intrebare simpla:
de ce e 2 cand aritmetica spune 1?

Si ceva despre limitele masuratorii: cele treizeci de scenarii de atunci n-au
miscat NICIO cifra la reparatia asta, fiindca niciunul nu punea vreodata un
consort sa inceteze sa guverneze in timp ce altii il urmau. Defectul a trebuit
gasit CITIND codul.

## Nava inamică

Un `AEnemyShipPawn` creat la pornire de `ASeaGameMode`, la 632 m de tine.
Aceeași cocă și aceleași tunuri ca ale tale, doar că la timonă stă
`AShipAIController`.

Are trei tactici:

| Tactică | Când | Ce face |
|---|---|---|
| apropiere | peste 550 m | prova spre tine, toate pânzele sus |
| angajare | sub 550 m | se pune de-a curmezișul ca să-ți prezinte bordul, ținând 320 m |
| retragere | cocă sub 30% | vântul în pupă și fuge |

Nu virează niciodată prin ochiul vântului: dacă drumul scurt până la cursul pe
care îl vrea ar trece prin vânt, pune cârma invers și vine prin pupă, ca o navă
cu vele pătrate adevărată. Fără asta rămânea împotmolită cu prova în vânt câte
optzeci de secunde: măsurat, 5 salve în 300 de secunde în loc de 14.

Partea care contează: **nu se poate întoarce oriunde**. Dacă direcția pe care o
vrea cade în vânt, nu încearcă un curs pe care o navă cu vele pătrate nu-l poate
ține: pune cursul strâns la 70° de vânt, ține mura minimum 45 de secunde, apoi
întoarce pe cealaltă. Adică merge în vânt în zigzag, ca un bric adevărat. E
același model de velatură ca al tău, deci ai aceleași limite ca ea.

Apare la 632 m, aproape în vântul tău. De asta prima apropiere durează câteva
minute: trebuie să facă bordee ca să ajungă la tine.

Când e în rază, alege bordul cu virajul mai scurt, dacă acel curs e navigabil,
și trage când ținta îi intră la 9° de travers, cât pot tunurile să se rotească.

Când o scufunzi, la 8 secunde apare alta, la același loc de pornire.

## Prăzile

Un negustor care coboară pavilionul e o **pradă**, şi valorează cât i-a rămas
uscat în cală: `valoare = marfă × (cocă rămasă / cocă întreagă)`. Marfa întreagă
e 1200 (`-ConvoyCargo=N`).

Asta pune preţ pe alegerea pe care tunurile o oferă de mult şi care până acum nu
costa nimic: **unde tragi**. Ţinut pe Shift, echipajele ochesc în greement —
cinci ghiulele sus îi rup velatura sub 0,6 şi coboară pavilionul cu coca
neatinsă, deci plăteşte **1200**, tot. Tras în cocă, îi trebuie şapte ghiulele
ca s-o aduci la 580 din 1000, si marfa udata plateste **696**.

Masurat, nu estimat: perechea `convoy_weather` / `prize_hull` difera intr-un
singur flag (`-AIAimHigh=0`) si da fix cifrele astea.

**Ce NU mai e adevarat, si a fost pana la recenzia din 16.09:** ca tirul in coca
ar fi drumul mai LENT. Parea asa - 83,1 s fata de 70,9 - dar numai fiindca
negustorul carase pana atunci opt guri de tun pentru care n-avea tunuri, deci
primele patru lovituri pe fiecare bord erau inghitite ca "scoate un tun" si nu-l
costau nicio coca. Cu gurile alea scoase, ambele feluri de a trage o opresc in
aceeasi clipa. Alegerea a ramas curata: **aceeasi treaba, alt pret.**

Punga se vede în panou sub rândul CONVOY şi se scrie la sfârşitul fiecărei
rulări (`PRIZELOG PURSE`), inclusiv într-o rulare fără convoi — un zero numărat.

### Magazia: ghiulelele se termina

**Magazia e FINITA - patruzeci de ghiulele, zece salve, cinci pe bord.** Asta e
decizia owner-ului din 16.09, si schimba ce fel de joc e: o lupta pe care o poti
pierde fiindca ai ramas fara e alta lupta.

Numarul vine din suita, nu din gust. In douazeci si patru de scenarii masurate
inamicul trage intre 0 si 20 de ghiulele in aproape toate, 24 intr-o urmarire
lunga, si **44 in `crew_fight`** - un duel de 360 de secunde. Patruzeci acopera
orice actiune scurta si se goleste exact in singurul loc unde o magazie ar
trebui sa conteze. Un numar mai mare n-ar lega nicaieri in suita, si un implicit
care nu leaga nicaieri nu se poate deosebi de lipsa lui.

`-Shot=N` si `-EnemyShot=N` schimba numarul; oricare pus pe **0** face magazia
navei aleia fara fund - asa se poate masura comportamentul de dinainte fata de
cel de acum.

O salva cheltuie cate o ghiulea de tun, **si plateste cat poate**. Cu trei
ghiulele si patru tunuri pleaca o salva de trei, iar al patrulea tun ramane
incarcat. Pana pe 19.09 aici era o garda agregata - "una pentru fiecare tun care
mai trage, altfel nimic" - si o nava cu trei ghiulele din patru refuza tacut sa
traga, fara ca nimic din ecran sa spuna de ce. Panoul are o bara SHOT cand exista
o magazie, si scrie MAGAZINE DRY cand nu mai ai NICIO ghiulea. Tot pe 19.09,
cateva ore: panoul a ramas pe regula veche, `shot < cate tunuri ai`, si strigase
MAGAZINE DRY cu rosu - in aceeasi stiva cu SHE IS GOING DOWN - la un capitan a
carui urmatoare comanda trimitea trei ghiulele. O avertizare care se aprinde cand
lucrul MERGE e mai rea decat niciuna: invata jucatorul sa ignore randul.

Masurat cu perechea `shot_short` / `shot_plenty`, un singur flag diferenta:
`own_guns_first` e **3** contra **4**. Aia e salva partiala, numarata.

**Si fiecare tun isi tine propriul ceas de reincarcare**, nu bordul. Un tun care
n-a tras nu asteapta dupa cei care au tras. Ceea ce trebuie spus limpede, fiindca
e usor de crezut mai mult: azi ceasurile nu se pot desincroniza decat printr-o
magazie prea scurta, iar dupa salva partiala magazia e goala oricum - deci
castigul se vede abia **dupa o reaprovizionare**, cand tunul tinut in rezerva
trage imediat si ceilalti trei inca se servesc. Structura e acolo si e probata;
momentul in care se simte in joc e ingust.

Portul vinde ghiulele cu 2 bucata - cel mai ieftin lucru de pe lista, si primul
cumparat la refit: un echipaj intreg pe o coca sanatoasa cu magazia goala e un
transport, nu o nava de lupta. Capitanul inamic pleaca spre port si cand magazia
scade sub un sfert.

Masurat, cu perechea `magazine_short` / `magazine_enough`, un singur flag
diferenta: cu **patru** ghiulele raider-ul trage O salva, ramane uscat, si
convoiul TRECE - `mission_result` 1, la 316,3 s; cu **opt** trage doua, opreste
un negustor, si actiunea se incheie la 71,0 s cu `mission_result` 2. Magazia
schimba deznodamantul, nu doar contabilitatea.

(Perechea s-a numit `magazine_dry` / `magazine_full` si a fost citata aici cu
cifrele alea inca doua saptamani dupa ce fusese redenumita si renumerotata.)

**Cum s-a putut face asta fara sa se mute toata suita.** Comentariul vechi de
aici sustinea ca garda TREBUIE sa fie inainte de bucla si totul-sau-nimic: bucla
trage doua numere aleatoare per tun (deriva si inaltimea), din sirul pe care
`-ShipSeed` il fixeaza, deci un tun care ar refuza tacut sa traga ar sari peste
extrageri si ar muta FIECARE ghiulea de dupa el. Avea dreptate despre pericol si
gresea despre singura iesire.

Ce trebuie protejat sunt EXTRAGERILE, nu nasterea ghiulelei. Un tun care isi ia
cele doua numere si **abia apoi** refuza costa exact cat a costat mereu. Refuzul
per tun sta dupa extrageri, numarul ramane 2 per tun montat, si sirul nu observa
nimic - prin constructie, nu prin noroc. Proba: cele 33 de scenarii vechi n-au
miscat **nicio** cifra.


### Ce cumpara punga

Portul vinde exact cele doua lucruri pe care marea nu ti le da inapoi:

| | pret | de ce nu se repara singur |
|---|---|---|
| un om | 20 | mortii nu se intorc, iar cei plecati cu o prada sunt plecati |
| un punct de coca | 0,5 | echipele de reparatii fac carma si catargele, **coca niciodata** |

O coca intreaga costa 500, adica mai putin de jumatate dintr-o prada (1200).
Echipajul de prada care ti-a luat-o - doisprezece oameni - costa 240 ca sa-l
inlocuiesti. Deci o prada dusa acasa plateste de doua ori oamenii care au
luat-o si mai ramane pentru jumatate de coca; o prada **scufundata** nu
plateste nimic.

Se plateste din **LANDED**, nu din PURSE: banii care au ajuns la chei, nu cei
pe care i-ai revendicat. Panoul arata `COFFERS`, adica ce a mai ramas.

Refitul **nu e instantaneu**: un om sau douazeci de puncte de coca la fiecare
jumatate de secunda petrecuta in rada. O nava ciuruita sta acolo vreo douazeci
de secunde - timp in care convoiul fuge. Banii sunt un cost, ceasul misiunii e
celalalt, si al doilea e cel care face din plecatul devreme o decizie.

Capitanul inamic are aceeasi doctrina cu `-AIRefit=1` (implicit STINSA): pleaca
spre port cand e cu doisprezece oameni sub complet sau sub 70% coca - **si are
cu ce plati**. Regula aia din urma nu e decorativa: masurat fara ea, o nava
lovita pornea spre port in primele secunde, inainte sa fi castigat un ban, si
statea intr-o rada goala toata partida fara sa vaneze nimic.

Masurat cap-coada, cu `refit_on`: prada luata la 138 s, acasa la 359 (1200 in
vistierie), pleaca spre port, ajunge la 764, cumpara **20 de oameni si 400 de
puncte de coca pentru 600**, si iese din nou in larg la 776 cu 600 ramasi.


### Portul: unde o pradă devine bani şi oamenii se întorc

`-Port=1` pune o radă prietenă pe apă. O pradă cu echipajul tău la bord **face
vela şi fuge spre ea** — pe exact acelaşi drum pe care un negustor îl navighează
spre radă, fiindcă asta şi e: un negustor cu altă destinaţie. Când intră în
radă, **banii sunt ai tăi** şi **oamenii se întorc pe puntea ta**.

Rada e aşezată implicit **sub vântul** convoiului (`-PortOffingM=`, implicit
900 m), şi nu din decor: o pradă e lucrată de doisprezece oameni acolo unde
şaizeci o navigau, iar doisprezece oameni nu duc o cocă încărcată în vânt.
`-PortX/-PortY` o pun unde vrei.

Panoul arată acum două cifre, nu una:

- **PURSE** — cât valorează prăzile la momentul în care au coborât pavilionul;
- **LANDED** — cât a ajuns efectiv la chei.

Sunt egale doar dacă toate prăzile au ajuns acasă. Măsurat: din două prăzi
luate în cinci sute de secunde, **una** ajunge (1200 din 2400), iar cei
doisprezece oameni întorşi duc tunurile raider-ului înapoi la viteză plină
(1,00 faţă de 0,75). Cealaltă e încă pe mare când se termină partida — şi asta
e o pierdere reală, nu o rotunjire.

### Stăpânirea: o pradă nu e a ta până nu ai oameni pe ea

O navă care a coborât pavilionul e **oprită**, nu **a ta**. Ca s-o iei, te apropii
la 150 m şi stai lângă ea douăzeci de secunde (cumulate, nu neîntrerupte) — apoi
pleacă **doisprezece oameni** la bordul ei, şi **nu se mai întorc**: o
navighează, nu-ţi mai servesc tunurile.

Acolo muşcă felia de echipaj. Şaizeci de oameni la bord, patruzeci şi opt la
tunuri pentru reîncărcare plină: **prima pradă e gratis, a doua nu**. Măsurat, cu
valorile implicite şi fără niciun flag de reglaj: raider-ul ia ambii negustori,
pleacă 24 de oameni, şi reîncărcarea ei scade la 36/48 = 0,75.

Există şi o **podea**: nu poţi coborî sub douăzeci de oameni pe puntea ta. Sub ea
boţii nu pleacă, prada rămâne doar oprită, şi căpitanul AI renunţă şi face vela
după patruzeci de secunde degeaba — nu stă lângă o navă pe care n-o poate lua.

Numărul care a decis distanţa de acostare e în log şi în linia de bază:
`prize_closest_m`. Cu doctrina stinsă, cât de aproape ajunge raider-ul de o navă
care a coborât pavilionul, natural, e **272 m** — de trei ori distanţa de
acostare. Adică fără doctrină nu s-ar lua NICIODATĂ o pradă, şi asta nu se putea
şti presupunând.

**Ce NU face încă:** punga nu cumpără nimic şi nu supravieţuieşte rulării, prada
nu se duce nicăieri (rămâne pe loc, cu oamenii tăi la bord), oamenii nu se mai
întorc niciodată, iar o navă scufundată după ce a coborât pavilionul rămâne
numărată ca oprită.

## Echipajul

Nava are **oameni**, nu doar lemn: șaizeci la plecare (un negustor, paisprezece).
O ghiulea îi omoară: două la o lovitură în cocă, trei la un tun scos din luptă,
unul sus în greement sau la cârmă. Eșuarea nu omoară pe nimeni. Pierderile se
adună pe toată partida și nu se întorc niciodată.

Oamenii rămași sunt împărțiți între **tunuri** și **reparații** (R, sau
`-ShipRepairShare=x`). Tunurile se reîncarcă atât de repede câți oameni au: opt
tunuri a câte șase oameni înseamnă patruzeci și opt, deci cu șaizeci la bord
primii doisprezece morți nu costă nimic la tunuri, și fiecare de după ei
încetinește reîncărcarea, până la un sfert din viteză cu nimeni. Sub `HANDS` din
panou vezi câți ai și unde sunt.

Echipa de reparații **înnoadă și matisește**: cârma întâi, apoi catargul mai
rău, cu 0,00015 din integritate pe om pe secundă — treizeci de oameni refac o
lovitură în greement (0,12) în vreo douăzeci și cinci de secunde, un catarg de
la 0,40 în vreo sută. Niciodată peste 0,85: o matiseală nu face un catarg
întreg. Coca nu se repară pe mare, deocamdată.

Căpitanul inamic face la fel: rănit și fără nimic de tras (în afara razei, sau
fugind), trimite jumătate din oameni sus; în rază, toți la tunuri.
`-AIRepair=0` îi oprește doctrina, ceea ce face mecanica măsurabilă: perechea
`crew_repair` / `crew_fight` pornește un inamic cu greementul la 0,40 la 1,5 km
de tine și diferă într-un singur flag. Cu reparații ajunge sub greement de
avarie (0,77) și trage prima salvă la 224,6 s; fără, ajunge cum a plecat și
trage la 255,9. Negustorul nu trimite pe nimeni sus în felia asta: ce-i strică
o salvă rămâne stricat, și de asta coboară pavilionul.

Ce a ieșit la iveală pe drum: în afara distanței de menținere căpitanul „ușura"
spre țintă cu un sfert de grad pe metru, adică la 380 m ținea un drum la 75° de
relevment — dădea roată unei nave staționare cu tunurile la 15° de travers, iar
arcul de tragere e 9°. Măsurat: nouăzeci de secunde fără o salvă, apoi cercul a
dus-o cu prova în vânt. Acum, în afara distanței de menținere, unghiul crește de
trei ori mai repede; înăuntru e neschimbat, deci toată familia `gunnery`, care
pornește înăuntru, a rămas exact unde era.

## Scufundarea

La integritate zero nava nu dispare și nu rămâne o țintă: se scufundă, în
patru faze pe care le vezi.

| Faza | Ce se întâmplă | Cât |
|---|---|---|
| inundare | lovitura fatală decide bordul: pierde portanța pe acel bord, se lasă, se înclină spre spărtură și se afundă cu prova sau pupa | ~12 s |
| punte inundată | stă cu puntea la nivelul apei, înclinarea se accentuează; ghiulelele trec prin ea, nicio navă nu se mai poate lovi de ea | 5 s |
| plonjare | ultima portanță se duce, alunecă sub apă cu ~2,5 m/s | ~12 s |
| epavă | la 30 m adâncime e ștearsă din lume | |

Nava care se scufundă nu mai navighează, nu mai trage și nu mai e țintă pentru
nimeni. Dacă e a ta: **înfrângere**, camera rămâne pe ea, iar după 35 de
secunde primești o navă nouă la punctul de start. Dacă e a inamicului:
**victorie**, iar la 8 secunde apare un inamic nou. Game mode-ul ține scorul.

În editor, consola (`~`) acceptă `Scuttle`: îți scufundă nava pe loc, ca să
vezi secvența fără să aștepți 17 lovituri.

## Verificare automată

Două fluxuri, fiindcă unul singur ar minți.

**`checks`** rulează pe un runner GitHub obișnuit, la fiecare push. Acolo NU
există Unreal — motorul e peste o sută de gigaocteți în spatele unei licenţe —
deci fluxul ăsta nu compilează nimic şi nu măsoară nimic din joc. Verifică ce se
poate verifica fără el: că fiecare script se parsează, că niciun fişier de care
depinde Unreal n-are BOM, că nimic care seamănă a secret n-a intrat în arbore,
că nimic regenerat de motor nu e urmărit de git, şi că texturile se construiesc,
ies identice bit cu bit la două rulări, şi au perioade care divid latura.

Şi mai face un lucru: **îşi demonstrează că poate ieşi roşu.** Un pas strică
intenţionat o textură şi cere verificărilor s-o prindă. O poartă pe care nimeni
n-a văzut-o vreodată picând e o poartă în care nimeni n-are motiv să aibă
încredere.

**`measure`** rulează jocul pe bune, fără interfaţă, şi compară numerele cu o
linie de bază din `tools/measurement_baseline.json`. Are nevoie de motor, deci
merge doar pe un runner propriu, pe o maşină care îl are. Cinci scenarii, toate
cu hazardul fixat: o salvă la distanţă cunoscută, o navă mânată pe plajă, una
care navighează sub cârmă, una în care o navă e sabordată dinadins, şi una cu
şase coci şi trei sloturi de siaj.

Ultimele două nu spun nimic despre tunuri. Există ca `ships_sunk` şi contoarele
de siaj să fie **diferite de zero undeva**: `ships_sunk` citea un şir pe care
jocul nu-l scrie niciodată (`sink=sinking`, când fazele se numesc
`afloat|flooding|foundering|plunging|wreck`), deci era pironit la zero şi poarta
nu putea ieşi roşie orice s-ar fi întâmplat în joc. Un contor care dă zero pe
toate scenariile nu se deosebeşte cu nimic de un contor stricat.

Rulabile şi local, fără GitHub:

```
python tools/ci_checks.py
python tools/ci_measure.py            compară cu linia de bază
python tools/ci_measure.py --record   o rescrie, deliberat
```

**O linie de bază lipsă nu e o diferenţă.** Scriptul spune „NEW, not broken" şi
iese roşu fiindcă n-a putut compara, nu fiindcă ceva s-a stricat — distincţia pe
care un `diff` care eşuează pe absenţă a ratat-o deja o dată în proiectul ăsta.

**Şi un număr care nu mai e măsurat nu e o potrivire.** Comparaţia merge pe
REUNIUNEA cheilor, nu doar pe cele culese acum: `lift_mean`, `wake_live_max` şi
`islands_built` se scriu doar dacă logul le poartă, deci o rulare care nu mai
tipăreşte `lift=` nu oferă `lift_mean` deloc — iar o buclă care umblă doar prin
ce-a cules tocmai acum n-ar ajunge niciodată la el şi ar raporta potrivire
curată pentru un joc care nu mai pluteşte. Se raportează separat de MOVED:
cauza şi remediul sunt altele. Comparaţia stă acum într-o funcţie proprie,
`compare()`, şi `ci_checks.py` o hrăneşte cu fixturi pe o maşină fără motor —
cele două defecte de mai sus n-ar fi putut fi prinse RULÂND poarta, fiindcă
rularea ei era exact ce producea lumina verde.

## Diagnostice din linia de comandă

Toate rulările de măsurare merg fără interfață:

```
UnrealEditor-Cmd.exe PirateSeas.uproject -game -NullRHI -unattended -nosound <flag-uri>
```

**Ca să compari două rulări, ai nevoie de trei flag-uri în plus:**
`-UseFixedTimeStep -FPS=60 -ShipSeed=1`. Fără ele rulările NU se repetă. Măsurat:
două rulări cu exact aceleași flag-uri și exact același cod au dat 5 și apoi 8
lovituri în greement. Cu pas fix, cele două loguri sunt identice bit cu bit
până la prima salvă și divergeau exact acolo — la singurul hazard din proiect,
împrăștierea tunurilor. Fizica și căpitanul erau dintotdeauna repetabile;
tunurile nu erau niciodată. Orice comparație înainte/după făcută pe o singură
pereche de rulări citea împrăștiere și credea că citește semnal.

| Flag | Ce face |
|---|---|
| `-ShipQuitAfter=N` | închide jocul după N secunde (game mode, timpul lumii) |
| `-ShipShotAfter=N` | captură de ecran din joc la N secunde |
| `-ShipShots=a,b,c` | MAI MULTE capturi, la secundele alese, numite după secundă |
| `-ShipShotCam=` | vantaj fix: `beam` `low` `bow` `far` `rig`; fără el camera stă unde nimereşte |
| `-ShipShotName=` | prefixul fişierelor, ca două galerii să stea alături |
| `-ShipShotYaw= -ShipShotPitch= -ShipShotArm= -ShipShotHeight= -ShipShotFov=` | suprascriu vantajul, cadru cu cadru |
| `-ShipFireTest=N` | o salvă la tribord la N secunde |
| `-ShipWindSweep=N` | rotește vântul complet în N secunde, cu toate pânzele sus (diagrama polară) |
| `-ShipSinkTest=N`, `-EnemySinkTest=N` | sabordează nava respectivă la N secunde, prin aceeași cale ca o ghiulea |
| `-ShipSinkSide=port` | spărtura de test la babord (implicit tribord) |
| `-ShipHullTest=N` | nava jucătorului pornește cu N integritate, ca o scufundare prin tir să dureze două salve |
| `-ShipRudderTest=N` | pune toată pânza și cârma la maxim de la N secunde, ca să se citească viteza de giraţie |
| `-ShipRigDamage=x`, `-ShipRudderDamage=x` | nava jucătorului pornește cu zona aia avariată (0 la 1) |
| `-ShipGunsDown=N` | N tunuri scoase pe fiecare bord de la start |
| `-ShipForeRigDamage=x`, `-ShipMainRigDamage=x` | fiecare catarg separat, ca panoul să poată fi verificat că le arată diferit |
| `-EnemyX= -EnemyY= -EnemyYaw=` | unde și cum apare inamicul |
| `-EnemyCount=N` | câte nave inamice, de la 0 la 8 (implicit 2; 0 = nimeni nu te vânează) |
| `-Convoy=N` | N negustori (0 la 6, implicit 0 = fără convoi şi fără misiune) |
| `-ConvoyNeed=N` | câţi trebuie opriţi ca să fie LUAT (implicit jumătate, rotunjit în sus) |
| `-ConvoyX= -ConvoyY=` | de unde iese convoiul (cm; implicit 120000, 150000) |
| `-ConvoyWindAngle=` | drumul lui, în grade faţă de direcţia DIN care bate vântul (implicit 90) |
| `-ConvoyRangeM=` | cât are de fugit până în radă (implicit 1500 m; rada e tăiată în cutia oceanului) |
| `-RaiderSide=weather\|lee` | aşază escadronul inamic în vântul convoiului sau sub el |
| `-RaiderOffingM=` | la câţi metri de convoi, de-a lungul vântului (implicit 800) |
| `-ConvoyStrikeTest=N` | primul negustor coboară pavilionul la N secunde, fără tunuri |
| `-AIRepair=0` | căpitanul inamic NU mai trimite oameni la reparații (implicit 1) |
| `-ShipRepairShare=x` | nava jucătorului pornește cu fracția x din oameni la reparații (0 la 0,75) |
| `-ConvoyCargo=N` | cât valorează marfa fiecărui negustor, întreagă (implicit 1200) |
| `-AIAimHigh=1\|0` | căpitanul inamic ochește MEREU în greement / MEREU în cocă (fără flag, decide singur) |
| `-AIPrize=1` | căpitanul inamic merge lângă o navă care a coborât pavilionul și trimite oameni (implicit NU) |
| `-PrizeCrew=N` | câți oameni pleacă la o pradă (implicit 12 — exact cei de prisos peste tunuri) |
| `-PrizeRangeM=N` | de la ce distanță pot ajunge bărcile (implicit 150 m) |
| `-PrizeBoatSeconds=N` | câte secunde lângă pradă le ia bărcilor (implicit 20, CUMULATE) |
| `-EnemyHands=N` | inamicul porneste cu N oameni ABOARD, complementul ramane 60 (doar navele Coroanei) |
| `-EnemyHull=N` | inamicul porneste cu N din 1000 coca (doar navele Coroanei) |
| `-AIRefit=1` | capitanul inamic pleaca spre port sa se refaca, daca are bani (implicit NU) |
| `-HandCost=N -HullPointCost=x` | preturile din port (implicit 20 si 0,5) |
| `-Shot=N` | magazia TA, N ghiulele (fara flag: 40; `-Shot=0` o face fara fund) |
| `-EnemyShot=N` | magazia inamicului (doar navele Coroanei) |
| `-ShotCost=N` | cat costa o ghiulea in port (implicit 2) |
| `-Port=1` | pune o radă prietenă unde prăzile se duc acasă (implicit NU există) |
| `-PortOffingM=N` | la câți metri SUB VÂNTUL convoiului e rada (implicit 900) |
| `-PortX= -PortY=` | poziția exactă a radei, dacă nu vrei una sub vânt |
| `-PortRadiusM=N` | cât de mare e rada (implicit 150 m) |
| `-Islands=N` | pune N insule pe mare, 1 la 8 (0 sau lipsă = niciuna) |
| `-IsleX= -IsleY= -IsleRadius=` | unde e și cât de mare (raza plajei, implicit 11000 cm) |
| `-ShipRunAground=N` | din secunda N, mână nava în insulă cu toate pânzele sus, apoi strânge pânza la 10 s după atingere |
| `-ShipSeed=N` | **fixează hazardul**, deci rularea se poate repeta |
| `-ShipInheritVel=0` | ghiuleaua NU mai moşteneşte viteza navei (comportamentul de dinainte de 13.09) |
| `-ShipLead=0` | tunurile nu mai anticipează mişcarea ţintei |
| `-ShipRangeBias=x` | cât de lung trag tunurile (implicit 1,04) |
| `-ShipSmoke=0` | stinge fumul de tun (implicit APRINS din 17364af) |
| `-ShipFlash=0` | stinge focul de la gura tunului (implicit APRINS) |
| `-ShipSplinters=0` | stinge aschiile de la o lovitura in cocca (implicit APRINSE) |
| `-ShipHoles=0` | stinge urmele loviturilor de pe cocca (implicit APRINSE) |
| `-WindBearing=N` | fixează DIRECȚIA vântului, altfel „mal sub vânt" nu e reproductibil |
| `-WindSpeed=N` | fixează și TĂRIA lui; fără asta două treceri peste același unghi sunt luate pe vreme diferită |
| `-ShipPolar=1` | polarul de regim STABILIZAT: ține cârma pe un cap, așteaptă până nava nu mai schimbă nimic, abia atunci scrie rândul |
| `-ShipPolarStep= -ShipPolarDwell=` | câte grade între rânduri, și cât i se dă să se așeze |
| `-AITackAbove=N` | viteza peste care căpitanul vine în vânt în loc să vireze prin pupă (implicit 4; o valoare uriașă readuce comportamentul vechi) |
| `-AIBeatMargin=N` | cât de departe de zona moartă ține bordeiul (implicit 22, adică 70 de grade) |
| `-AILookAhead=N` | secunde de privire înainte după uscat (implicit 0, adică deloc; 90 e valoarea care merge) |
| `-AILandClearance=N` | câtă apă vrea între drumul ei și banc (implicit 20000 cm) |
| `-Islands=N` | până la 8 insule, așezate DE-A CURMEZIȘUL vântului, deci o coastă |
| `-IsleSpread=` | cât de rare, implicit cât să se atingă bancurile |
| `-EnemyRigDamage= -EnemyRudderDamage=` | pornește inamicul avariat (o navă cu jumătate de pânză nu se poate smulge de pe un mal sub vânt) |

Rănile din eșuare se scriu `GROUNDLOG`, nu `SHOTLOG`: o rană de la fund nu e o
lovitură de tun și n-are ce căuta în socoteala tirului.

**Atenție la `-NullRHI`.** Cu el, panoul nu se desenează ȘI nu-și scrie logul,
fiindcă `DrawHUD` e blocat de motor când nu există randare. Logul `HUDLOG` se
scrie acum din `Tick`, deci merge și fără randare; dar dacă vrei să VEZI panoul,
scoate `-NullRHI` și folosește `-ShipShotAfter=N`.

**Atenție la citare în PowerShell.** Un flag cu zecimale trebuie pus între
ghilimele: `"-ShipRigDamage=0.62"`. Scris fără ghilimele, PowerShell îl
trunchiază la 0 și rularea pare că funcționează, doar cu alt rezultat. Flag-urile
cu numere întregi nu au problema asta.

Panoul scrie și el o linie pe secundă, `HUDLOG`, cu exact valorile pe care le
desenează, ca să se poată verifica cifrele din spatele pixelilor dintr-o rulare
care nu randează nimic.

Logul (`Saved/Logs/PirateSeas.log`) scrie `SHIPLOG` o dată pe secundă pe navă,
`SINKLOG` în plus cât timp se scufundă, `SHOTLOG` pe ghiulea, `AILOG` la 2 s
și `SEALOG` din game mode.

## Instrumentele

Nava are un panou, desenat integral în C++ pe canvas: fără widget-uri, fără
texturi de interfață, fiindcă proiectul se construiește prin script iar Python
nu poate scrie noduri Blueprint.

**Roza din dreapta sus nu arată nordul.** Arată unde e puterea, cu prova mereu
în sus:

| Ce vezi | Ce înseamnă |
|---|---|
| conturul palid | ce ar trage greementul întreg, pe fiecare curs |
| linia plină | ce trage acum, cu cât greement ți-a mai rămas |
| sectorul roșu | vântul în care pânzele flutură, nu poți naviga acolo |
| cele două linii verzi | unghiurile care câștigă cel mai repede teren în vânt |
| săgeata de pe inel | de unde bate vântul |
| linia din centru în sus | prova; devine roșie când ești în vânt |
| linia galbenă mai scurtă | drumul REAL, după derivă |

Diferența dintre contur și linia plină e exact ce ți-au tăiat tunurile.

Jos-stânga, **viteza** și, sub ea, cât teren câștigi sau pierzi **spre vânt**.
Al doilea număr e cel care contează când urci în vânt: poți merge cu șase noduri
și totuși să pierzi teren. Roșu înseamnă că pierzi.

Sub el, **starea navei**: coca, fiecare catarg separat, cârma și câtă pânză e
sus. Dreapta jos, **tunurile**: patru pastile pe bord, stinse când afetul e
distrus, plus bara de reîncărcare. Scrie „POINTED HIGH" cât ții Shift.

Peste tot, când e cazul: **IN IRONS**, **DISMASTED**, **RUDDER GONE**,
**SHE IS GOING DOWN**.

## Cat de sus stau tunurile

Masurat pe 18.09: gura de tun iesea la **19 centimetri** deasupra apei. Salva
pleca practic de la linia de plutire, si de aceea nava citea ca o barja cu
catarge. Cauza nu erau tunurile.

Nava e desenata in `Scripts/ship.py` cu bord liber 2,1 m si copastie 1,15 m
peste el, dar plutea cu originea la **-78 cm** medie: tot vasul statea un metru
sub linia lui de plutire, deci puntea la 1,1 m in loc de 2,1.

Doua cauze, amandoua reparate:

1. **Sferele de flotabilitate erau centrate pe originea cocii.** O sfera de raza
   320 are nevoie de 375 cm de imersiune ca sa-si duca partea din greutate, deci
   centrata pe linia de plutire nu poate echilibra decat scufundand originea.
   Coborate cu 80 cm, calibrat pe patru rulari - legea iese liniara,
   `restingZ ~= -78 + cadere`, si `-PontoonDrop=` o lasa masurabila din nou.
2. **`GGunPortZ` era 120**, adica un metru SUB puntea pe care stau tunurile.
   Acum 280: gura iese prin sabord, la 87 cm deasupra puntii.

| | inainte | dupa |
|---|---|---|
| originea cocii, medie | -78,4 cm | **+1,9 cm** |
| gura de tun deasupra apei | **19 cm** | **297 cm** |

N-a fost nevoie de marirea navei; proportiile erau bune, doar nu se vedeau.

Si consecinta, fiindca nu e cosmetica: ridicarea gurii cu 2,8 m **misca toata
balistica**, iar in `prize_hull` corsarul devine de doua ori mai eficient -
`prizes_taken` 1 -> 2, `purse_end` 696 -> 1392. Asta e o schimbare de echilibru,
nu o zecimala, si e inregistrata in baseline in acelasi commit care o provoaca.

## Chila
## Chila

Nava are un plan lateral: rezistă mult mai tare la mișcarea în lateral decât la
cea înainte. Fără el aluneca lateral la fel de ușor cum înainta, deriva ajungea
la 23 de grade și **nu putea urca deloc în vânt**, oricât de bine ar fi fost
pusă pe curs.

| unghi față de vânt | viteză | derivă | câștig în vânt |
|---|---|---|---|
| 44 și mai jos | 0,00 m/s | — | 0 (zona moartă) |
| 50 | 0,92 m/s | 6,2 | +0,51 m/s |
| 55 | 2,55 m/s | 5,7 | +1,24 m/s |
| 60 | 3,56 m/s | 6,1 | **+1,43 m/s** |
| 65 | 4,29 m/s | 6,6 | +1,32 m/s |
| 70 | 4,89 m/s | 7,2 | +1,04 m/s |
| 80 | 5,68 m/s | 8,4 | +0,07 m/s |
| 90 | 6,13 m/s | 9,4 | -1,12 m/s |
| 120 | 6,33 m/s | 8,8 | -4,07 m/s |
| **141** | **6,41 m/s** | 7,1 | -5,43 m/s |
| 180 | 6,00 m/s | 0,0 | -6,00 m/s |

**Tabelul ăsta e remăsurat 13.09 și e altul decât cel publicat până acum.** Cel
vechi era o măsurătoare TRANZITORIE: vântul se rotea două grade pe secundă în
timp ce nava încă accelera, iar capul ei era liber, deci unghiul din fiecare
rând se schimba din două motive fără legătură. Dădea 50° → 2,5 m/s acolo unde
nava chiar face 0,92, fiindcă intra în unghiul ăla cu viteza luată de la un curs
mai larg. Vezi `-ShipPolar` mai jos.

Ce s-a confirmat din tabelul vechi: **60 de grade e într-adevăr cel mai bun
unghi de urcare în vânt** (+1,43 m/s, față de +1,39 declarat), iar vârful de
viteză e la ~140 de grade. Ce s-a schimbat: la 50 de grade nava practic nu
merge, iar la 70 câștigă în vânt mult mai mult decât se credea (+1,04, nu +0,69).

Deriva de 6-9 grade e cât făcea o navă cu vele pătrate adevărată. Ca să urci în
vânt, ține prova pe la 60 de grade de el: acolo câștigi cel mai repede, chiar
dacă la 70 mergi mai tare. Peste 80 pierzi teren, oricât de repede ai merge.

**Cifra de +1,43 la 60 de grade e adevărată și totuși NU e unghiul la care
trebuie să bordeiască nimeni.** Măsurat, mutând bordeiul de la 70 la 60 de grade:
inamicul ajunge mai aproape de țintă (363 m față de 380) și trage **zero salve**
în loc de trei. Polarul descrie o navă care merge în linie dreaptă la nesfârșit;
o navă care bordeiază își petrece o parte din viață întorcându-se, iar la 60 de
grade merge prin apă cu 3,56 m/s în loc de 4,89 — deci întoarce mai încet,
cârmește mai prost și ajunge prea târziu ca să mai apuce să pună tunurile pe
bord. Bordeiul rămâne la 70 de grade.

Ce a scos la iveală întrebarea asta e altceva, și e mult mai mare. Vezi mai jos.

Chila se opune și rotirii, nu doar derapajului: cercul de giraţie e de 73 m,
cam două lungimi de navă.

## Cum se comportă velele

Nava e cu vele pătrate, deci se poartă ca un bric adevărat:

| Unghiul vântului | Ce se întâmplă |
|---|---|
| sub 48° | **în vânt**, pânzele flutură, nu faci deloc drum |
| 48° spre 90° | prinde treptat, până la 80% din forță |
| 90° spre 140° | zona bună, forță maximă cu vântul pe pupa-travers |
| 140° spre 180° | vânt din pupă, scade la 78% fiindcă velele din spate acoperă |

Unghiul se măsoară față de direcția **din care** bate vântul. Zero înseamnă că
prova e fix în vânt.

Vântul mai face două lucruri: împinge nava lateral, adică derivă, și o înclină
pe o bordură, proporțional cu cât de mult bate din travers.

Vântul nu e constant. Direcția și tăria oscilează lent, din sinusoide
suprapuse cu perioade care nu se repetă scurt, deci nu simți un ciclu.

## Ce conține proiectul

| Ce | Unde |
|---|---|
| nava, 7074 triunghiuri, 37,5 m, originea pe linia de plutire | `Content/Meshes/SM_PirateShip` |
| material părinte + opt instanțe (cocă, punte, lemn, lemn închis, velă, fier, parâmă, vegetație) | `Content/Materials/` |
| nivelul: ocean 5 km, zonă de apă, manager de plutire, cer, soare, ceață, nori | `Content/Maps/L_OpenSea` |
| suprafața mării: grilă radială de 30 880 de triunghiuri, 6 km | `Content/Meshes/SM_SeaSurface` |
| materialul mării: șase unde Gerstner în deplasare de vârfuri | `Content/Materials/M_Sea` |
| actorul care poartă marea și o ține la pas cu fizica | `Source/PirateSeas/OceanSurface.*` |
| nava, ca pawn fizic: plutire, vele, tunuri, integritate | `Source/PirateSeas/ShipPawn.*` |
| vântul lumii, o singură sursă de adevăr | `Source/PirateSeas/WindSubsystem.*` |
| ghiuleaua, corp fizic cu gravitație | `Source/PirateSeas/CannonBall.*` |
| nava inamică și căpitanul ei | `Source/PirateSeas/EnemyShipPawn.*`, `ShipAIController.*` |
| game mode: îți dă nava și creează inamicul | `Source/PirateSeas/SeaGameMode.*` |

## Cum funcționează nava

Rădăcina pawn-ului e o cutie de coliziune care simulează fizică, nu mesh-ul:
`UBuoyancyComponent` aplică forțele pe root, iar așa catargele și velele nu
intră niciodată în calculul fizic. Șase pontoane sferice pe linia de plutire
țin nava sus, centrul de masă e coborât 180 cm ca să se redreseze singură.

Cârma nu face nimic dacă nava stă pe loc, autoritatea ei crește cu viteza. Cu
pânză sus păstrează totuși un minim, ca un echipaj care bracează vergile: fără
asta o navă prinsă cu prova în vânt n-ar mai putea nici să se miște, nici să
vireze, niciodată.

## Lanțul C++ (funcțional din 10.09.2026)

Proiectul e acum proiect C++. Modulul e la `Source/PirateSeas` și compilează:

```
Build.bat PirateSeasEditor Win64 Development -Project=...\PirateSeas.uproject
Result: Succeeded
```

| Componentă | Versiune folosită |
|---|---|
| Visual Studio | Community 2026 (18.10.0) |
| Toolchain MSVC | 14.44.35228 |
| Windows SDK | 10.0.22621.0 |
| NETFXSDK | 4.8 |

Atenție dacă reinstalezi vreodată: folderul de toolset se numește `14.44.35207`,
dar compilatorul din el e `14.44.35228`. Intervalul interzis de Unreal este
`14.44.0-14.44.35210`, deci pare că îl prinde, dar nu-l prinde. Uită-te la ce
raportează UBT, nu la numele folderului.

## Ora din zi

`-Hour=17.5` mută soarele unde l-ar pune ora aia, şi ia cu el culoarea, puterea,
lumina cerului şi banda de expunere. Fără flag nu rulează nimic din toate astea
şi nivelul îşi păstrează lumina cu care a fost scris — felia e **opt-in**
dinadins: o oră care ar fi mutat pe tăcute aspectul livrat şi fiecare număr din
linia de bază ar fi fost o schimbare de lumină deghizată în funcţionalitate.

Soarele urcă după un sinus între răsărit şi apus şi mătură de la est la vest. Nu
e efemeridă, dar are cele două proprietăţi care contează: lumina rade pe apă la
capetele zilei şi vine din alt cadran la fiecare oră.

**Banda de expunere e ANCORATĂ în punctul măsurat, nu calculată.** Aspectul
livrat e 110.000 lux într-o bandă de 12,5–16 EV, obţinută prin baleiere şi
uitat la capturi; orice altă oră e banda aia mutată cu exact atâţia paşi cu câţi
s-a mutat lumina. O bandă calculată din principii — ce am scris prima dată — a
pus apusul cu două stopuri şi jumătate prea sus şi a transformat un asfinţit
corect colorat într-o siluetă. Aritmetica era bună; ancora era inventată.

## Vântul, în ce se îndoaie

Marea răspundea la vânt de azi-dimineaţă; plantele şi greementul nu răspundeau
deloc. Materialul navei mută acum vârfurile cu vântul — `SwayAmount` pe piesă,
peste `SwayHeightCm` de înălţime proprie — iar C++-ul împinge direcţia, viteza şi
un ceas în materiale dinamice: insula pentru plantele ei, fiecare navă pentru
cele şapte sloturi ale ei.

Forma contează mai mult decât amplitudinea. Planta e **îndoită sub vânt întâi**
şi oscilează în jurul îndoirii; îndoirea creşte cu viteza vântului; o rafală
lentă o umflă şi o lasă; iar deplasarea e scalată cu înălţimea deasupra originii
piesei, LA PĂTRAT, deci rădăcina nu se mişcă şi vârful se mişcă cel mai mult. O
frunză care alunecă lateral din rădăcină nu e vânt, e un mesh care se desface.

Tot greementul se mişcă la fel de mult (`0.10` peste 1800 cm): un sart e legat de
vârful catargului, deci dacă s-ar mişca de patru ori mai mult — ce dădea o citire
„parâmele sunt mai flexibile" — s-ar desprinde vizibil de vergă.

**Amplitudinea e fizică, nu grafică.** Şase centimetri pe metru pe secundă: la
14 m/s, coroana unui palmier de şase metri face vreo optzeci de centimetri. De
la distanţa de la care e privită insula, ăsta e un pixel-doi — şi asta e
corect. O versiune intermediară avea 2,2 metri şi arăta ca un ştergător de
parbriz.

`tools/png_diff.py` a apărut din felia asta: compară o CASETĂ din două capturi şi
dă un procent. L-am scris fiindcă „palmierii se mişcă în vânt" e exact genul de
afirmaţie pe care proiectul ăsta o ia greşit uitându-se la ea — şi primul lucru
pe care l-a spus a fost că nu se mişcă nimic. Avea dreptate: sonda de înălţime
legată la emissive era de 1,0 într-o scenă luminată cu 110.000 de lux, adică o
lumânare la amiază. Ridicată la 120.000 de niţi, a arătat că rampa era 1 peste
toată nava, fiindcă `ObjectBounds` întorcea zero pentru mesh-ul ăla.

## Ce rămâne de construit

- AI care știe de uscat: să nu se lase prins pe un mal sub vânt e REZOLVAT;
  rămâne că trage prin insulă, ceea ce README-ul de mai sus numește o decizie
  deliberată, nu un gol
- umbra de vânt în adăpostul insulei
- pânza nu fâlfâie: vela se îndoaie cu catargul, dar n-are mişcarea ei proprie
- cerul e tot cel implicit al motorului (`-Hour=` mută soarele şi expunerea,
  dar norii şi atmosfera rămân cum sunt), şi timpul nu curge în timpul unei
  partide
- niciun sunet
- coca nu poartă urme de lovitură: gaura se vede în cifre, nu pe lemn
- convoiul n-are prăzi, valoare a mărfii, escortă sau port adevărat (rada e un
  punct pe apă); negustorul nu ştie să vireze prin vânt, deci un drum aşezat în
  vânt l-ar putea prinde în irons
- echipajul e o singură rezervă împărțită între tunuri și reparații: manevra
  velelor nu e încă o stație, coca nu se repară pe mare, nimeni nu se
  recrutează și nimeni nu se plătește
- punga cumpara oameni, coca SI ghiulele, dar NU tunuri, nu nave mai bune, si
  nimic nu trece dintr-o rulare in alta - nu exista salvare
- preturile sunt alese ca sa se raporteze la valoarea unei prazi, nu masurate
  din ceva real
- nimeni nu recapturează o pradă care merge singură spre radă
- abordajul e AMÂNAT explicit de owner; echipajul + reparaţiile şi economia +
  progresia sunt următoarele ateliere, în ordinea asta

Reparate de când secțiunea asta a fost scrisă, și scoase din ea ca să nu fie
refăcute: magazia, care e FINITA implicit din 16.09 (decizia owner-ului, 40 de
ghiulele) si nu mai e o intrebare deschisa; negustorul, care primeste avarie in
coca de la o ghiulea in banda puntii lui de tun; portul, care nu mai cere un
convoi ca sa existe (`-Port=1` singur e de ajuns); mai multe insule odată (până
la opt, cu `-Islands=N`), și garda de apariție care acum ȘTIE de uscat — fiecare punct în care se poate naște o cocă e
verificat înainte să se pună uscat peste el.

## Stare verificată

Am rulat jocul fără interfață și am făcut capturi. Confirmate vizual de mine:

- nivelul se încarcă, `ASeaGameMode` e game mode-ul activ
- nava se randează, la scara corectă, cu materialele ei: cocă maro închis, vele
  crem, catarge de lemn, tunuri metalice
- cerul, norii volumetrici, atmosfera și ceața se randează
- ambele nave plutesc pe portanță, măsurat: forța de plutire / greutate = 1,00
  în medie, pescaj mediu 74 cm, legănate de o hulă de 1,4 m
- scufundarea, măsurată din log pe ambele borduri: listă spre spărtură, prova
  jos, punte inundată la ~12 s, epavă la 30 s, respawn la 35 s
- polarul de velatură, bătaia tunurilor (398–504 m) și înclinarea de recul sunt
  măsurate din log, nu presupuse
- inamicul se apropie, iese din vânt, se pune de-a curmezișul și deschide focul:
  8 salve, 22 de lovituri în 300 s, iar jucătorul e scufundat de tir real

- apa se vede și taie coca la linia de plutire, cu valuri care se mișcă

## Cum arată, şi cum se judecă asta

O pasă de grafică nu se poate verifica dintr-o linie de log, şi nici dintr-o
singură captură: un cadru de mare în mişcare, luat în clipa în care s-a nimerit
să ajungă rularea, e o anecdotă, nu o măsurătoare. De aceea prima piesă a feliei
n-a fost un pixel, ci **galeria**: `-ShipShots=8,25` face mai multe capturi, la
secunde alese, iar `-ShipShotCam=` aşază braţul camerei pe un vantaj fix şi îl ia
de pe controler. Cu `-UseFixedTimeStep -FPS=60 -ShipSeed=1` lumea e în aceeaşi
stare la secunda 20 în orice rulare, deci singurul lucru care diferă între două
galerii e randarea.

Instrumentul şi-a prins singur primul defect: `-ShipShots=8,25` a raportat
`1 frames requested: 8`, fiindcă `FParse::Value` se opreşte la virgulă. Linia
aceea numărată e tot ce a stat între o galerie de trei cadre şi una de unul.

### Texturile sunt generate, nu descărcate

`Scripts/textures.py` scrie douăzeci şi una de PNG-uri — lemn, pânză, fier, nisip,
rocă, iarbă, două foi de riduri de apă şi o mască de spumă. Nimic nu e luat de
undeva: totul e sintetizat, deci întreg aspectul rămâne reproductibil din sursă
şi nimic din arbore n-are licenţă ataşată.

Toate se îmbină EXACT. Trucul e că zgomotul se construieşte în domeniul
frecvenţei şi se transformă înapoi: un spectru pe frecvenţe întregi are
dimensiunea imaginii ca perioadă prin construcţie, deci cusătura nu poate exista.

### Nava n-are UV-uri

Verificat, nu presupus: `Scripts/ship.py` construieşte coca din secţiuni şi nu
creează niciodată un strat UV, în timp ce `island.py` are unul. O textură pusă
pe un mesh fără UV eşantionează acelaşi texel peste tot şi iese... o culoare
plată, adică exact ce trebuia să înlocuiască.

Deci proiecţie **biplanară în spaţiu local**: lateral `(x, z)` sau `(y, z)`, de
sus `(x, y)`, amestecate după cât de mult priveşte suprafaţa în sus.

**Care plan lateral contează.** `(x, z)` e corect pentru o cocă, a cărei lungime
merge pe X. E GREŞIT pentru o velă: vela e modelată în planul Y-Z, deci X e chiar
burta ei — iar folosind burta drept coordonată de textură se desenează curbele ei
de nivel, adică nişte **inele concentrice**. Inelul acela se vedea pe toate patru
velele din prima captură cu texturi şi l-am pus pe seama umbrei catargului, apoi
pe a cusăturii biplanare, apoi pe a bias-ului de umbră. Trei rulări. Era
coordonata. `SideSwap=1` pe instanţa velei, şi a dispărut. Două eşantioane, nu trei,
şi sunt exact cele două care ies corect — pe bordajul unei coci scândurile merg
de la prova la pupa, şi pe punte la fel. În spaţiu LOCAL, altfel lemnul ar înota
pe cocă în timp ce nava navighează.

### Cordajul

Nava are acum greement: sarturi de la parapet la cruce, scări de frânghie între
ele, straiuri înainte la bompres, pataraţine la pupă, braţe de la capătul verg­ii
şi şcote de la colţurile velelor. Opt sute cincizeci de patrulatere, toate
într-un **singur mesh**, fiindcă trei sute de funii înseamnă trei sute de apeluri
de desenare pentru opt sute de grame de cânepă.

Nu e detaliu — e diferenţa dintre o navă şi o jucărie. Ochiul ştie că nişte
catarge de douăzeci de metri nu pot sta în picioare fără ceva care să le ţină,
chiar dacă nu poate numi ce anume caută.

**Dincolo de 240 de metri cordajul dispare.** Funiile de trei centimetri sunt
sub un pixel la distanţa aia, iar geometria sub-pixel cu contrast mare nu se
mediază — scânteiază. Se stinge prin deplasare de vertex: dincolo de prag,
vârfurile funiilor sunt trase spre originea meshului până când toată funia e un
punct. E per-instanţă (`RopeCollapse`), deci coca, puntea şi velele au o
deplasare identic nulă şi nu pot fi atinse de asta.

Scările se opresc la 56% din sart, adică pe la verga de jos, unde pe o navă
adevărată e platforma. Duse până sus, traversează vela de deasupra şi tot
greementul devine desiş.

### Marea

Şase unde Gerstner, ca înainte. Ce s-a adăugat, în ordinea în care se vede:

- **riduri mărunte** din două foi de normale panoramate cu viteze diferite.
  Asta contează mai mult decât tot restul: între crestele unei hule cu şase unde
  suprafaţa e geometric PLATĂ, deci oglinda era răspunsul fizic corect la
  întrebarea pe care o punea materialul vechi;
- **spumă** pe creste, cu o mască de bule, aspră acolo unde e albă — încetarea
  reflexiei e motivul pentru care spuma se citeşte ca spumă şi nu ca vopsea;
- **împrăştiere**: creasta e subţire şi luminată prin ea, adâncitura nu.

### Siajul

O navă lasă acum o dâră de apă albă în urmă. Până la asta marea nu ştia că
trece cineva prin ea.

**Memoria stă în C++, ca firimituri.** Fiecare navă urmărită scapă un punct la
fiecare nouă metri de drum; materialul desenează un lanţ de capsule prin puncte
consecutive. Asta e tot rostul: firimiturile rămân **unde a fost apa**, deci
când pui cârma, siajul rămâne curbat în urmă. Un siaj calculat din capul navei
ar roti toţi cei optzeci de metri ai lui ca pe un băţ.

Alternativa evidentă e un render target ping-ponguit în fiecare cadru. E mai
capabil şi e compromisul greşit aici: pune starea pe GPU, unde instrumentul
întregului proiect — o rulare fără randare care citeşte linii de log — nu poate
s-o vadă. Aşa, fiecare număr e pe procesor şi se poate tipări.

Trei nave, fiindcă bugetul de parametri e real, iar siajul la care te uiţi e al
tău şi al celui mai apropiat adversar. A patra e **numărată şi scrisă în log**
(`ignored=`), nu aruncată în tăcere.

**Sub 1,2 m/s o navă nu lasă nimic** — o cocă nemişcată nu face apă albă, şi
fără poarta asta o navă oprită scapă toate firimiturile în acelaşi loc şi lanţul
se face punct. Se vede în log: o rulare în care jucătorul stă pe loc scrie
`ShipPawn_0:0` lângă inamici cu 8 firimituri fiecare. Aşa se deosebeşte „n-are
siaj fiindcă nu s-a mişcat" de „n-are siaj fiindcă e stricat".

### Vegetaţia

Doi arbuşti din Blender — un palmier înclinat şi o tufă bolovănoasă —
împrăştiaţi pe insulă la pornire prin trasare de rază, ţinuţi în două meshuri
instanţiate ierarhic: o sută patruzeci de plante sunt două apeluri de desenare.

Geometrie **opacă**, deliberat. Palmierul evident se face din câteva cartonaşe
cu alfa, dar proiectul n-a livrat niciodată un material mascat sau translucid şi
cele două încercări de a introduce unul au costat câte o sesiune fiecare. Frunze
solide sunt mai butucănoase decât cartonaşele, iar ce-i trebuia insulei era o
**siluetă ruptă** pe linia cerului — şi unei siluete nu-i pasă dacă frunza are
marginea decupată.

**Pragurile sunt CITITE din material, nu retastate.** `M_Island` decide
nisip-contra-iarbă din `SandTopCm`/`SandFadeCm` şi pământ-contra-rocă din
`RockSlopeStart`/`RockSlopeFade`; împrăştierea cere activului livrat exact cele
patru numere. O a doua copie a lor în C++ ar fi fost corectă exact până la prima
reglare a oricăruia, iar defectul ar fi fost un palmier stând pe nisip pictat —
vizibil doar într-o captură pe care nimeni n-o face. Logul scrie `4 of 4 numbers
read from the material`, deci o cădere tăcută pe valorile implicite s-ar vedea ca
`0 of 4`.

Respingerile se numără separat (`missed=`, `wrong height=`, `too steep=`),
fiindcă „n-a crescut nimic" are patru cauze şi trebuie deosebite fără o a doua
rulare.

Niciun material nou: plantele se desenează cu masterul navei, care proiectează
biplanar în spaţiu LOCAL — exact ce-i trebuie unui mesh fără UV-uri, şi ele n-au,
din acelaşi motiv pentru care nava n-are.

### Stropii

Ghiuleaua îi spune mării unde a lovit; marea ţine minte o secundă şi jumătate;
materialul desenează un inel de apă albă care se deschide şi se subţiază.

Al treilea efect pe acelaşi mecanism, după surful de la insule şi siaj: starea
în C++, împinsă ca parametri, desenată în material. Motivul e acelaşi de fiecare
dată — fiecare număr rămâne pe procesor, unde o rulare fără interfaţă îl poate
tipări. Logul scrie `splashes=8 live=N seen=N lost=N`, deci o salvă care pierde
jumătate din stropi pe inelul de opt sloturi **o spune**, în loc să pară doar
subţire.

Inelul e rupt de textura de bule BRUTĂ, nu de cea cu prag: pragul de 0,45 există
ca linia de surf să rămână continuă, şi e greşit aici — cu el inelul nu poate fi
găurit, iar patru cercuri perfecte pe apă se citesc ca geometrie, nu ca ghiulele
care cad.

### Gulerul şi braţele

Firimiturile sunt ISTORIE. Alte două lucruri au nevoie de unde e nava chiar în
cadrul ăsta:

**Gulerul** — apă albă în jurul cocii, acolo unde ea împinge marea deoparte
acum. Fără el siajul începea în larg, cu o navă plutind înaintea lui, nelegată.

**Braţele Kelvin** — cele două linii de apă spartă care pleacă din etravă sub un
unghi. Unghiul nu e o alegere: rezultatul lui Kelvin e că tiparul de valuri al
unei coci de deplasament stă într-un unghi de vreo nouăsprezece grade şi jumătate
de fiecare parte a drumului, **la orice viteză**.

Amândouă sunt aşezate pe **drumul** navei, nu pe capul ei: face şase până la nouă
grade de derivă, iar apa se închide în urma locului pe unde a trecut cu adevărat.

### Surful

Marea sparge acum pe uscat. Până la asta, apa era complet neştiutoare că
insulele există: aluneca peste o plajă şi pe un deal fără un fulg de alb, şi ăla
e lucrul care cel mai tare trăda că sunt două obiecte separate care doar se
suprapun.

C++ împinge în material poziţia fiecărei insule şi cele DOUĂ raze ale ei — raza
ţărmului şi marginea bancului — exact cele pe care le citeşte şi forţa de
eşuare, deci apa albă apare fix unde coca ar începe să simtă fundul. Opt insule;
a noua se aruncă **cu o linie în log**, nu tăcut.

Surful e o **bandă cu vârf**, nu o rampă: valurile sparg puţin în AFARA liniei
de ţărm, unde fundul se ridică destul cât să împiedice hula. Prima formă era o
rampă care atingea maximul la ţărm — adică îşi punea albul exact acolo unde
nisipul ascunde deja marea. Se vedea în capturi: fie o spălătură pătată în larg,
fie aproape nimic.

Textura de bule **sparge** surful, nu îl **filtrează**: înmulţită direct, o
gaură din textură făcea o gaură în linia de valuri. Are acum un prag sub care nu
coboară, deci linia rămâne continuă şi textura adaugă froth deasupra.

Pragul spumei **nu e o constantă**. C++ îl împinge în material din suma
amplitudinilor care chiar sunt în joc, ca fracţiune (`FoamCrestFraction`, 0,78).
A fost constantă întâi, şi o constantă nu poate fi corectă de două ori: 34 cm
era o creastă rezonabilă la 11 m/s şi a îngropat tot cadrul în alb la 13 — iar
albul a tras expunerea automată în jos până când nava însăşi s-a înnegrit.
Defectul s-a arătat ca „nava nu e luminată", care nu e locul unde era.

### Lumina

Nivelul avea soare, cer, ceaţă şi nori, şi **niciun volum de post-procesare** —
imaginea era developată de ce se nimerea să fie implicit. Acum: expunere
măsurată pe histogramă, îngrădită între stopuri; o curbă filmică cu vârf şi
umăr; Lumen la calitate ridicată, fiindcă marea E în cea mai mare parte reflexie
şi o reflexie rezolvată în opt raze e o pată — pata era exact reproşul.

**Unităţile sunt reale.** `DefaultEngine.ini` are
`ExtendDefaultLuminanceRange`, deci soarele se dă în lucşi adevăraţi:
**110 000**. Prima trecere a pus 7 — un număr plauzibil împrumutat din
convenţia cealaltă — şi scena a ieşit neagră.

## Fumul de tun — FUNCŢIONEAZĂ, şi tot stins implicit

Mecanismul e întreg şi funcţionează: un `AGunSmoke` per TUN (nu per salvă, ca
patru pufuri să facă un banc de fum de unsprezece metri de-a lungul bordului),
treizeci de cărţi instanţiate, jet cu frecare în primele trei zecimi, ridicare,
umflare, derivă sub vânt cu rampă şi forfecare, rotire per carte, plafon de
patruzeci şi opt de pufuri vii cu tăierea **scrisă în log**. Nelegat de navă:
fumul rămâne în urmă, cum face fumul.

Materialul e primul translucid, neluminat şi instanţiat din proiect. Fiecare
capacitate a fost **sondată înainte** (`Scripts/probe_material_api.py`), fiindcă
niciuna nu mai fusese folosită aici. Iar `blend_mode` şi `shading_model` se
citesc ÎNAPOI şi se asertează: helper-ul `sp()` al proiectului înghite un eşec
într-o linie WARN, deci un material rămas opac ar fi raportat „ok" şi ar fi
randat o placă gri.

**Acum arată a fum:** o vălătucire întunecată la gura tunului, ruptă şi cu
şuviţe, care se subţiază şi derivă sub vânt. **APRINS implicit din 17364af**, după
ce s-a văzut că era subexpus, nu întunecat. Livrarea a fost stânjenită exact de
motivul scris aici cât a fost stins: fiecare măsurătoare de tir din proiect fusese
luată fără el, deci aprinderea a cerut o reînregistrare DELIBERATĂ a liniei de
bază, în acelaşi commit. `-ShipSmoke=0` cumpără lumea veche înapoi.

**Măsurat că nu atinge tirul:** aceeaşi rulare cu fum stins şi cu fum pornit dă
**56 de linii de tir şi 11 lovituri în amândouă**. Fumul îşi trage cele treizeci
de numere aleatoare din fluxul LUI, semănat din indexul loviturii, nu din fluxul
global pe care `-ShipSeed` îl fixează.

### Trei erori de aritmetică şi una de arhitectură

- **Cărţile erau cât norul.** 260–400 cm într-un nor de 300 cm: plăci
  coincidente nu pot forma decât un perete. Acum 30 de cărţi de 175 cm semănate
  printr-o bilă (rădăcină cubică, ca să umple uniform, nu să se înghesuie pe
  margine).
- **Pragul de erodare era peste ce putea atinge zgomotul.** Două câmpuri de
  turbulenţă ÎNMULŢITE au media 0,12; pragul pornea de la 0,36. Alfa era zero
  din primul cadru — fumul era invizibil matematic înainte să fie desenat.
  Sumă ponderată în loc de produs, şi pragul mutat în intervalul real.
- **Alfa aplicată de două ori** în varianta aditivă (o dată de mine în culoare,
  o dată de blend): 0,085 la pătrat înseamnă 0,007 pe carte.
- **Aditivul nu se desena deloc.** Alegerea aditivă era raţionată — treizeci de
  cărţi într-un component instanţiat nu se sortează pe adâncime, iar adunarea e
  comutativă, deci n-are problemă de ordine. Măsurat însă: după repararea
  flagului, fiecare build aditiv a randat NIMIC, iar fiecare build translucid a
  randat cărţile. Argumentul era bun şi motorul nu e de acord cu el; câştigă
  motorul.

### DOUĂ capcane care au invalidat măsurători, nu doar fumul

**1. Materialul era ÎNLOCUIT în tăcere.** Un material trebuie să DECLARE că
poate fi folosit pe meshuri instanţiate. Al meu n-o făcea, iar motorul punea
materialul implicit şi spunea asta o singură dată, într-o linie care nu conţine
cuvintele „failed to compile":

    Material ...MID_M_GunSmoke_0 missing bUsedWithInstancedStaticMeshes=True!
    Default Material will be used in game.

Ore de reglat opacitatea, mărimea cărţilor, numărul lor, sursa de zgomot şi
modul de amestec — pe un material care **nu era niciodată pe ecran**. Căutasem în
log formularea pe care o ştiam de la mare („Failed to compile Material"); asta
are alt text. Acum `gunsmoke_material.py` pune flagul ŞI îl citeşte înapoi.

Ce a găsit-o: o sondă cu **o singură variabilă** — emisiv roşu aprins — care s-a
întors GRI. Un amestec aditiv nu poate desena mai închis decât cerul din spate,
iar ăsta desena mai închis; contradicţia aia a forţat întrebarea „e ăsta măcar
materialul meu?", pe care nimic altceva n-o pusese.

**2. Prima rulare de după o reconstrucţie de material NU arată materialul nou.**
Măsurat, nu bănuit: acelaşi material, trei rulări identice la rând — prima dă un
PNG diferit, a doua şi a treia sunt de acord între ele. Orice comparaţie
înainte/după făcută dintr-o singură rulare imediat după ce scriptul a rescris
materialul citeşte o stare veche. **Rulează de două ori şi compară a doua cu a
doua.**

Ce s-a aflat pe drum, fiindcă a costat şase rulări: eşantionarea din textură
ieşea **exact media texturii**, adică plat. Cinci cauze eliminate una câte una —
împachetarea canalelor, sRGB, compresia în tonuri de gri (sufixul `_M` din
convenţia proiectului aruncă tot ce nu e primul canal), tiling-ul coordonatei,
streamingul. Niciuna. Înlocuit cu **zgomot procedural** — care nu cere UV-uri,
nici setări de import, nici streaming — şi erodarea a pornit imediat. Vina era
în calea texturii; care anume rămâne neştiut.

## Limitări cunoscute

- Suprafața mării e desenată de proiect, nu de plugin-ul Water. Plugin-ul
  raportează totul sănătos (zonă, corp de apă, mesh de informații, materiale,
  vizibilitate) și nu desenează niciun triunghi: aceeași cadră cu
  `r.Water.WaterMesh.EnableRendering` pornit și oprit e identică. Plutirea
  folosește în continuare plugin-ul; doar imaginea e a noastră, din aceleași
  șase unde.
- Stropii sunt un inel de apă albă pe suprafaţă, nu o coloană care sare în sus:
  marea se desfăşoară, nu se ridică.
- Surful e un **inel geometric**: nu ştie din ce parte bate vântul, deci sparge
  la fel de tare şi în adăpostul insulei ca pe partea expusă.
- Ridurile de apă se văd că se repetă în zare, ca un moar.
- Velele au acum formă de velă: capul e legat drept de vergă pe toată lungimea
  lui, poala e LIBERĂ şi acolo se umflă cel mai mult, iar adâncimea e o fracţiune
  din LĂŢIME (11%), nu un număr fix de metri care făcea o velă de nouă metri şi
  una de şapte la fel de adânci.
- Roca nu se arată niciodată pe insulă, fiindcă dealul e prea blând ca să treacă
  de pragul de pantă — măsurat, nu bănuit: împrăştierea vegetaţiei raportează
  `too steep=0`, adică niciun punct din toată insula nu e destul de abrupt.
- Riduri de apă se văd că se repetă în zare, ca o dungă orizontală.
- Navele se lovesc doar cu ghiulele, nu una de alta.
- Cutia de coliziune a oceanului e un corp fizic real (WorldDynamic, blochează
  pawn-ii). Coca răspunde cu Overlap la WorldDynamic tocmai ca să NU stea pe
  ea: până pe 11.09 stătea, iar plutirea ducea doar 43% din greutate. Orice
  geometrie viitoare (insule) pe alt canal decât WorldDynamic.

## Cum a fost construit

Scripturile care generează totul, în ordinea rulării:

1. `ship.py` în Blender: modelează coca prin secțiuni transversale, generează
   punte, parapet, catarge, vele bombate, bompres, cabină și tunuri, apoi
   exportă FBX.
2. `build_game.py` în Unreal: importă FBX-ul, construiește pawn-ul și game
   mode-ul prin `SubobjectDataSubsystem`, creează nivelul cu ocean și cer.

Ambele sunt idempotente și pot fi rulate din nou.
