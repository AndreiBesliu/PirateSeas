#include "OceanSurface.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GerstnerWaterWaves.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "Island.h"
#include "GameFramework/PlayerController.h"
#include "ShipPawn.h"
#include "WaterWaves.h"

AOceanSurface::AOceanSurface()
{
	PrimaryActorTick.bCanEverTick = true;

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);
	// The sea is scenery. It must never stop a cannonball, and it must never
	// be something a hull can rest on: that mistake cost this project two days
	// once already, with the ocean's collision box holding the ships up.
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetGenerateOverlapEvents(false);
	// Six kilometres of water casting shadows costs a great deal and buys
	// nothing: there is nothing under it.
	Surface->SetCastShadow(false);
	Surface->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SeaMesh(
		TEXT("/Game/Meshes/SM_SeaSurface.SM_SeaSurface"));
	if (SeaMesh.Succeeded())
	{
		Surface->SetStaticMesh(SeaMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SeaMat(
		TEXT("/Game/Materials/M_Sea.M_Sea"));
	if (SeaMat.Succeeded())
	{
		Surface->SetMaterial(0, SeaMat.Object);
	}
}

void AOceanSurface::BeginPlay()
{
	Super::BeginPlay();

	SeaMaterial = Surface ? Surface->CreateAndSetMaterialInstanceDynamic(0) : nullptr;

	// Three knobs in the Wake category were read by NOBODY: not by this file,
	// not by any other, and not pushed into the material that has a parameter of
	// the same name waiting for them. Turning CollarWidthCm in the details panel
	// changed no pixel, while its two siblings on the lines above it did - so
	// half the collar was owned by the actor and half by the material, and the
	// panel said nothing about which half.
	//
	// They are constants, so they are pushed ONCE. And read BACK: setting a
	// parameter the material does not have is silently a no-op, which is exactly
	// how a wired knob becomes a dead knob again the day somebody renames it in
	// sea_material.py.
	if (SeaMaterial)
	{
		const TPair<const TCHAR*, float> Knobs[] = {
			TPair<const TCHAR*, float>(TEXT("CollarWidthCm"), CollarWidthCm),
			TPair<const TCHAR*, float>(TEXT("BowArmLengthCm"), BowArmLengthCm),
			TPair<const TCHAR*, float>(TEXT("BowArmHalfWidthCm"), BowArmHalfWidthCm),
		};
		FString Unwired;
		for (const TPair<const TCHAR*, float>& K : Knobs)
		{
			SeaMaterial->SetScalarParameterValue(FName(K.Key), K.Value);
			float Back = 0.f;
			if (!SeaMaterial->GetScalarParameterValue(FName(K.Key), Back)
				|| !FMath::IsNearlyEqual(Back, K.Value))
			{
				Unwired += FString::Printf(TEXT("%s "), K.Key);
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG wakeknobs collar_w=%.0f arm_len=%.0f arm_w=%.0f unwired=%s"),
			CollarWidthCm, BowArmLengthCm, BowArmHalfWidthCm,
			Unwired.IsEmpty() ? TEXT("none") : *Unwired);
	}

	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		if (UWaterBodyComponent* Component = It->GetWaterBodyComponent())
		{
			Water = Component;
			break;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("SEALOG surface mesh=%s material=%s water=%s"),
		(Surface && Surface->GetStaticMesh()) ? *Surface->GetStaticMesh()->GetName() : TEXT("NONE"),
		SeaMaterial ? *SeaMaterial->GetName() : TEXT("NONE"),
		Water ? *Water->GetName() : TEXT("NONE"));

	PushWaves();
}

void AOceanSurface::PushWaves()
{
	if (bWavesPushed || !SeaMaterial || !Water)
	{
		return;
	}

	// The wave set is built once, inside the waves object, and the ship adds
	// it a moment after BeginPlay, so this may have to wait a beat.
	UWaterWavesBase* Base = Water->GetWaterWaves();
	UGerstnerWaterWaves* Gerstner = Cast<UGerstnerWaterWaves>(Base);
	if (!Gerstner && Base)
	{
		Gerstner = Cast<UGerstnerWaterWaves>(Base->GetWaterWaves());
	}
	if (!Gerstner)
	{
		return;
	}

	const TArray<FGerstnerWave>& Waves = Gerstner->GetGerstnerWaves();
	if (Waves.Num() == 0)
	{
		return;
	}

	const int32 Count = FMath::Min(Waves.Num(), MaterialWaveCount);
	float Drawn = 0.f, Total = 0.f;
	for (int32 i = 0; i < Waves.Num(); ++i)
	{
		Total += Waves[i].Amplitude;
	}
	for (int32 i = 0; i < MaterialWaveCount; ++i)
	{
		FLinearColor Packed(0.f, 0.f, 0.f, 0.f);
		float QOverK = 0.f;
		if (i < Count)
		{
			const FGerstnerWave& W = Waves[i];
			// (WaveVector.X, WaveVector.Y, WaveSpeed, Amplitude): exactly what
			// GetWaveOffsetAtPosition reads, so the drawn surface and the
			// queried surface are the same surface.
			Packed = FLinearColor(W.WaveVector.X, W.WaveVector.Y, W.WaveSpeed, W.Amplitude);
			// Q divided by the wave number, so the material's horizontal term
			// can reuse the wave vector without normalising it.
			const float K = W.WaveVector.Size();
			QOverK = (K > KINDA_SMALL_NUMBER) ? (W.Q / K) : 0.f;
			Drawn += W.Amplitude;
		}
		SeaMaterial->SetVectorParameterValue(FName(*FString::Printf(TEXT("WaveA%d"), i)), Packed);
		SeaMaterial->SetScalarParameterValue(FName(*FString::Printf(TEXT("WaveQK%d"), i)), QOverK);
	}

	// Where the white water starts, in centimetres, derived from THIS sea
	// rather than fixed in the material.
	//
	// It was a constant first, and a constant cannot be right twice: 34 cm was
	// a reasonable top-of-the-crest in one breeze and buried the whole frame in
	// white in another, which then dragged the auto-exposure down until the ship
	// herself went black. The defect showed up as "the ship is unlit", which is
	// not where it was.
	//
	// Then it was a FRACTION OF THE SUM OF THE AMPLITUDES, which was worse,
	// because it looked right. That sum - 109 cm for this set - is the height a
	// crest reaches only if all six waves peak at the same point at the same
	// instant, which a set of independent phases never does. The crest is a sum
	// of six cosines: it has a standard deviation, sigma = sqrt(sum A^2 / 2) =
	// 34 cm, and 0.78 of the sum is 2.5 sigma - three pixels in a thousand - so
	// the sea had NO WHITECAPS AT ALL, and the top of the ramp (the sum itself,
	// 3.2 sigma) was not reachable by arithmetic. The log read healthy the whole
	// time: 85 looks like a sane fraction of 109 until you ask what 109 is.
	//
	// So the threshold is keyed to the crest's own spread, and the coverage it
	// produces is MEASURED below rather than reasoned about.
	float SumSq = 0.f;
	for (int32 i = 0; i < Count; ++i)
	{
		SumSq += FMath::Square(Waves[i].Amplitude);
	}
	const float Sigma = FMath::Sqrt(FMath::Max(SumSq * 0.5f, KINDA_SMALL_NUMBER));
	const float FoamStart = FoamCrestSigmas * Sigma;
	const float FoamRange = FMath::Max(8.f, (FoamFullSigmas - FoamCrestSigmas) * Sigma);
	SeaMaterial->SetScalarParameterValue(TEXT("FoamStartCm"), FoamStart);
	SeaMaterial->SetScalarParameterValue(TEXT("FoamRangeCm"), FoamRange);

	// The colour ramp reads the SAME crest signal as the foam, and it was still
	// a frozen 150 cm after the foam was keyed to the crest's spread. Two terms
	// on one signal; one was converted and the other was not. The floor is there
	// because a calm must not divide by something near zero and turn the sea
	// into two flat bands of the authored colours.
	const float ScatterRange = FMath::Max(25.f, ScatterCrestSigmas * Sigma);
	SeaMaterial->SetScalarParameterValue(TEXT("ScatterRangeCm"), ScatterRange);

	// HOW MUCH OF THE SEA ACTUALLY BREAKS. The same six waves the material was
	// just handed, evaluated on a lattice across twenty kilometres of water at
	// t=0, counted against the threshold. One number, and it is the number that
	// would have said "0.3%" when the comment said "the top fifth" - which no
	// amount of reading either of them was ever going to say.
	int32 Over = 0, Sampled = 0;
	TArray<float> Heights;
	Heights.Reserve(64 * 64);
	for (int32 iy = 0; iy < 64; ++iy)
	{
		for (int32 ix = 0; ix < 64; ++ix)
		{
			// Deliberately not a round lattice step: a grid whose spacing shares
			// a factor with a wavelength samples the same phase over and over.
			const FVector2D P(ix * 317.f, iy * 293.f);
			float H = 0.f;
			for (int32 i = 0; i < Count; ++i)
			{
				H += Waves[i].Amplitude
					* FMath::Cos(Waves[i].WaveVector.X * P.X
						+ Waves[i].WaveVector.Y * P.Y);
			}
			++Sampled;
			Over += (H > FoamStart) ? 1 : 0;
			Heights.Add(H);
		}
	}
	const float BreakingPct = Sampled > 0 ? 100.f * Over / Sampled : 0.f;

	// And how much of the colour ramp the sea actually travels: the same
	// lattice, put through the material's own scatter expression. This is the
	// number that was silently swinging from a quarter of the ramp in a calm to
	// nine tenths in a gale while the parameter behind it never moved, and it is
	// the one that must now stay PUT while the centimetres follow the wind.
	Heights.Sort();
	float ScatterSpan = 0.f;
	if (Heights.Num() > 0)
	{
		auto Ramp = [ScatterRange](float Height)
		{
			return FMath::Clamp(0.5f + Height / (2.f * ScatterRange), 0.f, 1.f);
		};
		ScatterSpan = Ramp(Heights[Heights.Num() / 100])
			- Ramp(Heights[(Heights.Num() * 99) / 100]);
		ScatterSpan = FMath::Abs(ScatterSpan);
	}

	bWavesPushed = true;
	UE_LOG(LogTemp, Display,
		TEXT("SEALOG surface waves drawn=%d of %d, amplitude %.0f of %.0f cm (%.0f%%) "
			 "sigma=%.0f foam>=%.0f cm over %.0f, breaking on %.1f%% of the sea, "
			 "scatter>=%.0f cm span %.2f"),
		Count, Waves.Num(), Drawn, Total, Total > 0.f ? 100.f * Drawn / Total : 0.f,
		Sigma, FoamStart, FoamRange, BreakingPct, ScatterRange, ScatterSpan);
}

void AOceanSurface::PushIslands()
{
	if (bIslandsPushed || !SeaMaterial || !GetWorld())
	{
		return;
	}

	// Where the land is, so the sea can break on it. Until now the water was
	// completely unaware that islands existed: it slid over a beach and up a
	// hillside without a fleck of white, which is the single thing that most
	// gives away that the two are separate objects that happen to overlap.
	int32 Found = 0, Dropped = 0;
	float Radii[MaterialIslandCount] = {};
	FLinearColor Packed[MaterialIslandCount];
	for (int32 i = 0; i < MaterialIslandCount; ++i)
	{
		Packed[i] = FLinearColor(0.f, 0.f, 0.f, 0.f);
	}

	for (TActorIterator<AIsland> It(GetWorld()); It; ++It)
	{
		const AIsland* Isle = *It;
		if (!IsValid(Isle))
		{
			continue;
		}
		if (Found >= MaterialIslandCount)
		{
			++Dropped;
			continue;
		}
		const FVector C = Isle->GetActorLocation();
		// (X, Y, shore radius, shoal outer radius) - the same two radii the
		// grounding force reads, so the white water appears exactly where the
		// hull would start to feel the bottom.
		Packed[Found] = FLinearColor(C.X, C.Y,
			Isle->GetShoreRadiusCm(), Isle->GetShoalOuterCm());
		Radii[Found] = Isle->GetShoreRadiusCm();
		++Found;
	}

	for (int32 i = 0; i < MaterialIslandCount; ++i)
	{
		SeaMaterial->SetVectorParameterValue(
			FName(*FString::Printf(TEXT("Isle%d"), i)), Packed[i]);
	}

	// Counted, not assumed: a world with no islands must report zero here, and
	// a run that shows surf with this line reading 0 is a bug in the material,
	// not in the sea.
	// Latched on SUCCESS, not on the attempt. It used to latch either way, so a
	// surface that ticked once before any island existed gave up for good and no
	// island spawned afterwards could ever reach the material - while the header
	// above described exactly that case as the reason for the retry.
	bIslandsPushed = (Found > 0);
	UE_LOG(LogTemp, Display,
		TEXT("SEALOG surf against %d islands (dropped=%d, first shore r=%.0f cm)"),
		Found, Dropped, Found > 0 ? Radii[0] : 0.f);
}

void AOceanSurface::ReportSplash(UWorld* World, const FVector& Where)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AOceanSurface> It(World); It; ++It)
	{
		AOceanSurface* Sea = *It;
		if (!IsValid(Sea))
		{
			continue;
		}
		Sea->Splashes.SetNum(MaxSplashes);
		FSplash& S = Sea->Splashes[Sea->NextSplash % MaxSplashes];
		if (S.bAlive)
		{
			// The oldest slot was still in use. Counted rather than quietly
			// reused: a broadside that loses half its splashes should say so.
			++Sea->SplashesOverwritten;
		}
		S.Where = Where;
		S.Age = 0.f;
		S.bAlive = true;
		Sea->NextSplash = (Sea->NextSplash + 1) % MaxSplashes;
		++Sea->SplashesSeen;
		return;
	}
}

void AOceanSurface::UpdateWake(float DeltaSeconds)
{
	if (!SeaMaterial || !GetWorld())
	{
		return;
	}

	// Who to follow. The player first, then whoever is nearest her - the wake
	// you watch is your own and your opponent's.
	TArray<AShipPawn*> Ships;
	AShipPawn* Player = nullptr;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		Player = Cast<AShipPawn>(PC->GetPawn());
	}
	// STILL ON THE SURFACE, not "not holed". IsSunk() is HullIntegrity <= 0, so
	// it flips the instant a ball goes through her - with twenty-five seconds of
	// flooding still to run, during which she is measurably still under way at
	// four metres a second. Following on IsSunk() meant a hull visibly making
	// way with no wake, no collar and no bow arms at all, in a glassy sea, for
	// half a minute. She lays water until the deck goes under; after that she
	// stops laying and what she left ages out behind her.
	const auto OnSurface = [](const AShipPawn* S)
	{
		return S->GetSinkPhase() <= ESinkPhase::Flooding;
	};
	if (Player && OnSurface(Player))
	{
		Ships.Add(Player);
	}

	TArray<AShipPawn*> Others;
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		AShipPawn* S = *It;
		if (IsValid(S) && S != Player && OnSurface(S))
		{
			Others.Add(S);
		}
	}
	const FVector From = Player ? Player->GetActorLocation() : GetActorLocation();
	Others.Sort([From](const AShipPawn& A, const AShipPawn& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), From)
			< FVector::DistSquared2D(B.GetActorLocation(), From);
	});
	const int32 Ignored = FMath::Max(0, (Ships.Num() + Others.Num()) - MaxWakeShips);
	for (AShipPawn* S : Others)
	{
		if (Ships.Num() >= MaxWakeShips)
		{
			break;
		}
		Ships.Add(S);
	}

	// A trail belongs to a SHIP, by identity, and keeps its slot for as long as
	// that ship is followed. It used to belong to a place in the list instead -
	// Trails[i] for the i-th nearest ship - and the comment above it claimed
	// otherwise, which is how it survived a reading. Two failures came out of
	// that, and neither would have shown up in any count:
	//
	//   Two ships swapping distance rank swapped slots, each found a stranger's
	//   trail in its place, and both wakes were thrown away and started again -
	//   in open water, from nothing, for no reason a viewer could see.
	//
	//   Worse: a ship that sank out of the middle of the list left the ships
	//   behind it shifted DOWN one slot, so the last one was re-bound lower
	//   while its old slot still held its crumbs and still had a live ship in
	//   it. The cleanup pass keeps that slot (its ship is followed), the update
	//   pass never reaches it (it is past Ships.Num()), so its ages stop
	//   advancing while it is still packed and pushed every frame: a wake
	//   frozen in the water, at full strength, for the rest of the game.
	Trails.SetNum(MaxWakeShips);

	// The water does not care whether we are still watching. A trail AGES
	// wherever it lies - that is its own pass over every slot, not a step inside
	// the loop over followed ships, because the moment ageing lives inside that
	// loop a slot the loop does not reach stops ageing while it is still drawn.
	// The trail is only thrown away when it has aged to nothing.
	//
	// It used to be erased the instant its ship left the followed set: seventy
	// metres of white water gone in one frame, for a ship that was still there.
	for (FWakeTrail& T : Trails)
	{
		T.bAged = false;
		if (!T.Ship.IsValid())
		{
			// The actor is gone - the wreck was destroyed. Her wake is still in
			// the water and goes on fading; only the binding is dropped.
			T.Ship = nullptr;
		}
		if (T.Crumbs.Num() > 0)
		{
			for (float& A : T.Ages)
			{
				A += DeltaSeconds;
			}
			while (T.Ages.Num() > 0 && T.Ages.Last() > WakeLifeSeconds)
			{
				T.Ages.Pop();
				T.Crumbs.Pop();
			}
			T.bAged = true;
		}
		if (T.Crumbs.Num() == 0 && !Ships.Contains(Cast<AShipPawn>(T.Ship.Get())))
		{
			T.Ship = nullptr;
			T.bHasDropped = false;
		}
	}

	// A slot bound to a ship that is no longer followed, but still holding
	// crumbs, is a wake fading behind her - not a free slot. Taking one is
	// legitimate when a new ship needs somewhere to go, and it is COUNTED as a
	// steal so it can be told apart from the thing that must never happen:
	// taking a slot away from a ship that IS still followed, which is what rank
	// binding did on every distance swap.
	for (AShipPawn* S : Ships)
	{
		FWakeTrail* Found = Trails.FindByPredicate(
			[S](const FWakeTrail& C) { return C.Ship.Get() == S; });
		if (!Found)
		{
			Found = Trails.FindByPredicate(
				[](const FWakeTrail& C)
				{ return C.Ship.Get() == nullptr && C.Crumbs.Num() == 0; });
		}
		if (!Found)
		{
			// Nothing empty. Take the fading trail with the OLDEST head crumb -
			// the one nearest to going anyway - rather than leave a ship with no
			// wake for the thirteen seconds it takes a slot to clear.
			float Oldest = -1.f;
			for (FWakeTrail& C : Trails)
			{
				AShipPawn* Held = Cast<AShipPawn>(C.Ship.Get());
				if (Held && Ships.Contains(Held))
				{
					continue;
				}
				const float Head = C.Ages.Num() > 0 ? C.Ages[0] : TNumericLimits<float>::Max();
				if (Head > Oldest)
				{
					Oldest = Head;
					Found = &C;
				}
			}
		}
		if (!Found)
		{
			// No slot at all for a followed ship. Ships is capped at
			// MaxWakeShips and there are MaxWakeShips slots, so this cannot
			// happen - and "cannot happen" is what the last version of this said
			// too, so it is counted rather than assumed.
			++WakeDiscarded;
			continue;
		}
		if (Found->Ship.Get() != S)
		{
			// The one place a trail is ever thrown away, so the accounting lives
			// here. Taking a fading trail from a ship nobody is following any
			// more is a STEAL and is fine. Taking one from a ship that is STILL
			// FOLLOWED is the rank-binding bug: two ships swap distance rank,
			// each finds a stranger in its slot, and both wakes vanish in open
			// water. That must never happen, so it is counted separately - and
			// the counter was proved by putting rank binding back and watching
			// it go red, rather than by being trusted.
			if (Found->Crumbs.Num() > 0)
			{
				AShipPawn* Held = Cast<AShipPawn>(Found->Ship.Get());
				if (Held && Ships.Contains(Held))
				{
					++WakeDiscarded;
				}
				else
				{
					++WakeStolen;
				}
			}
			Found->Ship = S;
			Found->Crumbs.Reset();
			Found->Ages.Reset();
			Found->bHasDropped = false;
		}
		FWakeTrail& T = *Found;

		const FVector Here = S->GetActorLocation();
		const float SpeedCmS = S->GetVelocity().Size2D();
		const bool bFarEnough = !T.bHasDropped
			|| FVector::Dist2D(Here, T.LastDrop) >= WakeSpacingCm;
		if (SpeedCmS >= WakeMinSpeedCmS && bFarEnough)
		{
			// Dropped at the STERN, not at the centre of mass: a wake that
			// starts amidships is drawn through the hull. Just clear of the
			// transom - fourteen metres left a visible gap between the ship and
			// the head of her own wake; nine puts the first crumb where the
			// water actually closes behind her.
			const FVector Astern = Here - S->GetActorForwardVector() * 900.f;
			T.Crumbs.Insert(FVector(Astern.X, Astern.Y, 0.f), 0);
			T.Ages.Insert(0.f, 0);
			T.LastDrop = Here;
			T.bHasDropped = true;
			// This trail's clock is current: the crumb just laid is zero seconds
			// old. Without this the FIRST crumb dropped into an empty slot counts
			// as un-aged and the stranded counter fires on it - which is exactly
			// what it did, reading 1 in the gunnery scenario on the first run
			// after it was written. A counter that cries on correct behaviour is
			// the same defect as one that cannot cry at all.
			T.bAged = true;
			while (T.Crumbs.Num() > CrumbsPerShip)
			{
				T.Crumbs.Pop();
				T.Ages.Pop();
			}
		}
	}

	// Counted rather than trusted, because the bug they describe is invisible in
	// every other number the wake prints: the frozen trail was live, well-formed
	// and the right length.
	//
	// `stranded` used to read "a slot with crumbs and no ship", which the pass
	// above forbids by construction - every path that cleared the ship cleared
	// the crumbs in the same block, so the counter was nailed to zero, could not
	// fire for the very bug it was written for (a frozen trail HAS a ship), and
	// would have shipped a green light over a wake standing still in open water.
	// It now reads what actually goes wrong: crumbs that are being DRAWN while
	// nobody advanced their clock this frame. Put the ageing back inside the
	// followed-ships loop and this goes red on the first frame a slot is missed.
	for (int32 a = 0; a < Trails.Num(); ++a)
	{
		if (Trails[a].Crumbs.Num() > 0 && !Trails[a].bAged)
		{
			++WakeStranded;
		}
		for (int32 b = a + 1; b < Trails.Num(); ++b)
		{
			if (Trails[a].Ship.Get() != nullptr
				&& Trails[a].Ship.Get() == Trails[b].Ship.Get())
			{
				++WakeDoubled;
			}
		}
	}

	// Pack and push. Absent crumbs go out with strength zero, and the material
	// draws nothing for a segment whose either end is dead - which is what
	// keeps a world with no ships from growing a wake.
	int32 Live = 0;
	for (int32 s = 0; s < MaxWakeShips; ++s)
	{
		for (int32 k = 0; k < CrumbsPerShip; ++k)
		{
			FLinearColor Packed(0.f, 0.f, 0.f, 0.f);
			if (Trails.IsValidIndex(s) && Trails[s].Crumbs.IsValidIndex(k))
			{
				const float Age = Trails[s].Ages[k];
				const float Strength = FMath::Clamp(
					1.f - Age / FMath::Max(0.01f, WakeLifeSeconds), 0.f, 1.f);
				// (x, y, strength, half-width). The trail widens as it ages,
				// the way a real wake fans out astern.
				Packed = FLinearColor(Trails[s].Crumbs[k].X, Trails[s].Crumbs[k].Y,
					Strength, WakeHalfWidthCm + WakeSpreadCmPerSecond * Age);
				if (Strength > 0.f)
				{
					++Live;
				}
			}
			SeaMaterial->SetVectorParameterValue(
				FName(*FString::Printf(TEXT("WakePt%d"), s * CrumbsPerShip + k)), Packed);
		}
	}

	// Splashes: age them, retire them, push them.
	Splashes.SetNum(MaxSplashes);
	int32 LiveSplashes = 0;
	for (int32 i = 0; i < MaxSplashes; ++i)
	{
		FSplash& S = Splashes[i];
		FLinearColor Packed(0.f, 0.f, 0.f, 0.f);
		if (S.bAlive)
		{
			S.Age += DeltaSeconds;
			if (S.Age >= SplashLifeSeconds)
			{
				S.bAlive = false;
			}
			else
			{
				const float Age01 = S.Age / FMath::Max(0.01f, SplashLifeSeconds);
				Packed = FLinearColor(S.Where.X, S.Where.Y, Age01, SplashRadiusCm);
				++LiveSplashes;
			}
		}
		SeaMaterial->SetVectorParameterValue(
			FName(*FString::Printf(TEXT("Splash%d"), i)), Packed);
	}

	// The live part: collar and bow arms, from where each ship is THIS frame.
	// Packed A = (x, y, trackX, trackY), B = (half-length, half-beam, strength,
	// tan of the Kelvin half-angle).
	// Off the TRAIL's ship, not off the followed list, so the slot that draws a
	// ship's crumbs also draws her collar - and a hull that has stopped being
	// followed keeps hers for as long as she is still on the surface.
	for (int32 s = 0; s < MaxWakeShips; ++s)
	{
		FLinearColor A(0.f, 0.f, 0.f, 0.f);
		FLinearColor B(0.f, 0.f, 0.f, 0.f);
		AShipPawn* Laid = Trails.IsValidIndex(s)
			? Cast<AShipPawn>(Trails[s].Ship.Get()) : nullptr;
		// The phase, not IsSunk(): IsSunk() flips at the holing instant, so
		// gating on it switches the collar off in one frame under a ship that is
		// still making way. The collar and the bow arms are drawn in world XY
		// with no notion of the hull's depth, so a plunging ship would otherwise
		// paint surface foam from twenty-eight metres down.
		if (Laid && OnSurface(Laid))
		{
			const FVector Where = Laid->GetActorLocation();
			FVector Track = Laid->GetVelocity();
			Track.Z = 0.f;
			// Her TRACK, not her heading: she makes leeway, and the water
			// closes behind where she actually went. Falls back to the heading
			// when she is barely moving, so the collar does not spin.
			const float SpeedCmS = Track.Size();
			const FVector Dir = (SpeedCmS > 1.f)
				? Track / SpeedCmS
				: Laid->GetActorForwardVector().GetSafeNormal2D();
			const float Strength = FMath::Clamp(
				SpeedCmS / FMath::Max(1.f, WakeFullSpeedCmS), 0.f, 1.f);
			A = FLinearColor(Where.X, Where.Y, Dir.X, Dir.Y);
			B = FLinearColor(CollarHalfLengthCm, CollarHalfBeamCm, Strength,
				FMath::Tan(FMath::DegreesToRadians(BowArmAngleDeg)));
		}
		SeaMaterial->SetVectorParameterValue(
			FName(*FString::Printf(TEXT("WakeShip%dA"), s)), A);
		SeaMaterial->SetVectorParameterValue(
			FName(*FString::Printf(TEXT("WakeShip%dB"), s)), B);
	}

	// Counted, not assumed - and reported once a second rather than never, so a
	// wake that stops being laid can be seen to have stopped.
	static float Since = 0.f;
	Since += DeltaSeconds;
	if (Since >= 1.f)
	{
		Since = 0.f;
		FString Names;
		for (int32 s = 0; s < Trails.Num(); ++s)
		{
			if (Trails[s].Ship.IsValid())
			{
				Names += FString::Printf(TEXT("%s:%d "),
					*Trails[s].Ship->GetName(), Trails[s].Crumbs.Num());
			}
		}
		// `of 24` and `splashes=8` were compile-time constants printed in the
		// shape of measurements - 198 log lines across every scenario, one
		// distinct value each. They are gone; what replaces them can move:
		// `slots` is how many trails hold water, and `shortest` is the shortest
		// of them, which is the number that collapses when a trail is frozen or
		// mis-bound while `live` sits on its ceiling of 24.
		int32 Slots = 0, Shortest = MAX_int32;
		for (const FWakeTrail& T : Trails)
		{
			if (T.Crumbs.Num() > 0)
			{
				++Slots;
				Shortest = FMath::Min(Shortest, T.Crumbs.Num());
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("WAKELOG live=%d slots=%d shortest=%d tracked=%s ignored=%d "
				 "stranded=%d doubled=%d stolen=%d discarded=%d "
				 "alive=%d seen=%d lost=%d"),
			Live, Slots, Slots > 0 ? Shortest : 0, *Names, Ignored,
			WakeStranded, WakeDoubled, WakeStolen, WakeDiscarded,
			LiveSplashes, SplashesSeen, SplashesOverwritten);
	}
}

AActor* AOceanSurface::GetFocus() const
{
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			return Pawn;
		}
		// Between a sinking and a respawn the controller holds nothing, so
		// fall back on where it is looking rather than snapping the sea home.
		if (PC->PlayerCameraManager)
		{
			return PC->PlayerCameraManager;
		}
	}
	return nullptr;
}

void AOceanSurface::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bWavesPushed)
	{
		RetryTimer += DeltaSeconds;
		if (RetryTimer >= 0.5f)
		{
			RetryTimer = 0.f;
			PushWaves();
		}
	}

	if (!SeaMaterial || !Water)
	{
		return;
	}

	// The same clock the buoyancy uses, so the crest under the bow is the
	// crest the bow rides.
	SeaMaterial->SetScalarParameterValue(TEXT("WaveTime"), Water->GetWaveReferenceTime());
	PushIslands();
	UpdateWake(DeltaSeconds);

	if (const AActor* Focus = GetFocus())
	{
		const FVector Where = Focus->GetActorLocation();
		const float Step = FMath::Max(1.f, FollowStepCm);
		const FVector Snapped(
			FMath::GridSnap(Where.X, Step),
			FMath::GridSnap(Where.Y, Step),
			0.f);
		if (!Snapped.Equals(GetActorLocation()))
		{
			SetActorLocation(Snapped);
		}
	}
}
