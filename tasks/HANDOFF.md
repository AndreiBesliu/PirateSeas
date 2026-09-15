# Unde e proiectul, pe scurt

Scris ca o sesiune nouă (sau un model nou) să poată continua fără să recitească
DEVLOG-ul de 3000 de rânduri. Starea de la 15.09.2026, după commit-ul „convoiul" (al doilea din arcul de
mecanică).

## Ce e construit

**Bucla completă:** navighezi cu pânză şi cârmă (W/S trim, A/D cârmă, mouse doar
camera), tragi salve pe bord (Q/E, Shift = tir înalt), încasezi avarii pe zone,
te scufunzi în patru faze şi reapari. Un căpitan inamic AI navighează, schimbă
bordul, trage şi ştie de maluri sub vânt. Insule cu plajă, hău, vegetaţie şi
eşuare. Vânt care conduce şi vela, şi marea.

**Grafica** (arc încheiat, se va relua mai târziu): 21 de texturi sintetizate cu
numpy, trei plane de proiecţie în spaţiu de instanţă, normale din gradienţi,
mare cu şase valuri Gerstner care urmează vântul, spumă pe creste keyed la sigma,
siaj cu firimituri + guler + braţe Kelvin, stropi, surf la ţărm, cordaj cu LOD,
vânt în plante şi greement, `-Hour=` pentru ora din zi.

## Cum se lucrează aici (obligatoriu)

- **Totul prin script.** C++ în `Source/PirateSeas`, materiale ca grafuri Python
  în `Scripts/*.py`, meshuri din Blender, texturi din numpy. Editorul nu se
  deschide niciodată. Python-ul Unreal NU poate scrie noduri Blueprint sau
  Niagara.
- **Build:** `Build.bat PirateSeasEditor Win64 Development -Project=<abs>.uproject`
- **Un script de material:** `.\Scripts\run_py.ps1 -Script <x>.py -Tag "<filtru>"`
- **Porţile:**
  - `python tools/ci_checks.py` — fără motor, rulează şi în CI hosted
  - `python tools/ci_measure.py` — 10 scenarii headless vs `tools/measurement_baseline.json`
  - `python tools/ci_measure.py --record` — rescrie linia de bază, DELIBERAT, în
    acelaşi commit care o mişcă
  - `python tools/png_diff.py a.png b.png x0 y0 x1 y1` — compară o casetă
- **Reproductibilitate:** `-UseFixedTimeStep -FPS=60 -ShipSeed=1` plus vântul, pe
  fiecare rulare. Fără toate patru, două rulări identice dau numere diferite.
- **Capturi:** scoate `-NullRHI` şi adaugă `-ShipShots=20,21 -ShipShotCam=beam`.
  Prima rulare după ce reconstruieşti un material arată starea VECHE — rulează
  de două ori.

## Reguli care au costat ceva ca să fie învăţate

1. **O poartă care nu poate ieşi roşie e cel mai grav defect din proiect.** Un
   contor de eveniment rar se ZĂVORĂŞTE (membru cumulativ), nu se eşantionează:
   contoarele de siaj erau locale pe cadru şi linia se tipărea o dată pe secundă.
2. **Proba e MUTAŢIA:** pune defectul la loc, măsoară, scoate-l, măsoară. Un zero
   fără perechea asta nu înseamnă nimic.
3. **O valoare măsurată bate una derivată**, chiar când derivarea e corectă —
   banda de expunere calculată din principii a făcut din apus o siluetă.
4. **Un parametru pe care nimeni nu-l împinge e un buton mort**, şi a seta unul
   pe care materialul nu-l are e tăcut un no-op. Citeşte ÎNAPOI.
5. **Diferenţa de imagine** poate răspunde „e deplasat?", nu „se vede mişcând?"
   (TAA suprimă exact zonele cu vectori de mişcare).
6. **Sondele de depanare se scalează cu scena:** emissive 1,0 e invizibil la
   110.000 lux.
7. **Un scenariu cu două jumătăţi care trebuie să iasă DIFERIT** e singura
   probă că o mecanică înseamnă ceva: convoiul a ieşit identic de pe ambele
   părţi ale vântului de trei ori la rând (raider prea lent, negustor prea
   rapid, ţintă care sărea). Fiecare din ele ar fi trecut o poartă „nu crapă".
8. **Aceeaşi polară nu poate prinde aceeaşi polară.** O vânătoare se
   dimensionează măsurând-o (viteză, timp până în radă, timp până la prima
   salvă), nu alegând numere care sună bine.

## Arcul de MECANICĂ (în curs)

Grafica e parcată de owner („deocamdată arată ok"). Owner-ul a ales, prin
întrebare directă: **obiective**, apoi **echipaj + reparaţii**, apoi **economie
+ progresie**. **Abordajul e amânat explicit** („mai vedem cu abordajul") — nu-l
construi nesolicitat. Autonomie „ca până acum": deciziile de design le iau eu,
măsor tot, judecăţile vizuale merg în OWNER_VERIFY.

Livrat:
1. **Apartenenţa** (`ed65e93`): `EShipAllegiance {Player, Crown, Merchant}` pe
   clasă, `IsHostileTo()` într-un singur loc. Toate cele 10 scenarii nemişcate.
2. **Convoiul**: `AMerchantShipPawn` (încărcat, 55% viteză), coborârea
   pavilionului (`Strike()` pe ramura de avarii), rada, `-Convoy= -RaiderSide=`,
   misiune LUAT / A TRECUT / NEREZOLVAT cu o singură linie `CONVOYLOG MISSION`,
   contoare zăvorâte gauge/lee/beat. Perechea `convoy_weather` / `convoy_lee`
   trebuie să iasă DIFERIT. Şi două reparaţii la căpitan, fără de care convoiul
   era de neluat de pe nicio parte: urmărirea unui fugar (histereză pe distanţă)
   şi ţinta păstrată până iese din luptă.

Următorul: echipaj + reparaţii (un cronometru de ţinut poziţia lângă o navă
care a coborât pavilionul e o operaţiune de echipaj în tot afară de nume), apoi
economie + progresie (valoarea mărfii, prăzi).

## Ce aşteaptă ochiul owner-ului

`OWNER_VERIFY.md` are 24 de puncte; **16–24 n-au fost confirmate niciodată** —
sunt judecăţi vizuale pe care nu le pot face eu. Punctul 20 (fumul de tun) e o
întrebare deschisă: merită pornit implicit?

`tasks/REVIEW2_OPEN.md`: recenzia a doua e închisă complet, 34 din 34.
