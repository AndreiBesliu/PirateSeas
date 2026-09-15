# Recenzia a doua: toate cele 34 de constatări, închise

A doua pasă adversarială (15.09.2026) a raportat 34 de constatări. Douăsprezece
au fost verificate adversarial în ziua aia — toate au supravieţuit — şi toate au
fost reparate. Restul de 22 fuseseră raportate dar NU verificate, şi fişierul
ăsta le-a ţinut lista ca să nu dispară tăcut.

Sunt închise acum, toate. Cum:

## 6 erau deja reparate când s-a scris lista

Reparate în aceleaşi commit-uri care închideau cele douăsprezece verificate:
MI_Foliage pe un singur plan, spaţiul de instanţă pe meshuri instanţiate,
normala rocii insulei, prospeţimea logului de măsurare, `groundings` care număra
linii, şi rândul din README despre `-Islands`.

## 9 au fost trimise la verificare, câte un agent fiecare

Împotriva arborelui de ATUNCI, nu a celui despre care fuseseră scrise — patru
commit-uri se mutaseră sub ele. **Opt încă ţineau. Una nu ţinuse niciodată.**

Cele opt sunt reparate: controlul negativ din `checks.yml` care putea trece din
motivul greşit, linia ţărmului scalată faţă de vopsea, anticipaţia care scădea o
viteză pe care ghiuleaua n-o primea, lovitura fără ţintă notată ca lovitură în
plin, zăvorul din `PushIslands`, constantele tipărite ca măsurători în WAKELOG,
cele trei butoane moarte de siaj, şi `ScatterRangeCm`.

**RESPINSĂ, şi scrisă aici tocmai ca să nu fie „reparată" din greşeală mai
târziu:** `measure.yml` „şterge 2,9 GB înainte de fiecare build". Aşa e, şi
ASTA E IDEEA — un build la rece e ce face măsurătoarea onestă. Nu pune cache, nu
pune `clean: false`, nu ridica timeout-ul.

## 7 erau despre documente, şi toate erau adevărate

Numere pe care documentele le publicau şi codul nu le mai avea de mult. Măsurate
una câte una, apoi corectate:

| ce scria | ce e | unde |
|---|---|---|
| 4654 triunghiuri | **7074** | README, tabelul proiectului |
| şase instanţe de material | **opt** | README, acelaşi tabel |
| şaptesprezece PNG-uri | **douăzeci şi unu** | README, secţiunea de texturi |
| optsprezece cărţi de fum | **treizeci** | README, secţiunea de fum |
| „mai mult de o insulă" nefăcut | făcut (până la opt) | README, „ce rămâne" |
| garda de apariţie nu ştie de uscat | ştie | README, „ce rămâne" |
| cordajul n-are LOD | are, de la 240 m | OWNER_VERIFY 19 |

Plus trei pe care le-am găsit singur trecând prin OWNER_VERIFY: punctul 3 îţi
cerea să confirmi un throttle şi o cocă rotită din mouse (comenzile navei-jucărie
din prima săptămână), punctul 5 zicea „şase materiale" şi punea vina pe importul
FBX, iar punctul 6 îţi cerea o decizie despre Visual Studio luată acum o lună.

## Ce rămâne

Din recenzia a doua, nimic.

Ce nu e acoperit de ea, şi nu se pretinde că e: lucrurile din OWNER_VERIFY care
aşteaptă ochiul owner-ului (punctele 16-22), runner-ul self-hosted pentru
`measure.yml` care încă nu există, şi lista onestă de goluri grafice din
OWNER_VERIFY 18 — nimic nu se mişcă în vânt, cerul e cel implicit, niciun sunet,
coca nu poartă urme de lovitură.
