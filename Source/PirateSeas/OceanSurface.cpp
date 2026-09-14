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
	// rather than fixed in the material. Drawn is the sum of the drawn
	// amplitudes, which is the highest a crest can heap to, so the threshold
	// is a FRACTION of what this wind can actually build.
	//
	// It was a constant first, and a constant cannot be right twice: 34 cm was
	// a reasonable top-of-the-crest in an 11 m/s breeze and buried the whole
	// frame in white at 13, which then dragged the auto-exposure down until
	// the ship herself went black. The defect showed up as "the ship is
	// unlit", which is not where it was.
	const float FoamStart = FoamCrestFraction * Drawn;
	const float FoamRange = FMath::Max(8.f, (1.f - FoamCrestFraction) * Drawn);
	SeaMaterial->SetScalarParameterValue(TEXT("FoamStartCm"), FoamStart);
	SeaMaterial->SetScalarParameterValue(TEXT("FoamRangeCm"), FoamRange);

	bWavesPushed = true;
	UE_LOG(LogTemp, Display,
		TEXT("SEALOG surface waves drawn=%d of %d, amplitude %.0f of %.0f cm (%.0f%%) foam>=%.0f cm over %.0f"),
		Count, Waves.Num(), Drawn, Total, Total > 0.f ? 100.f * Drawn / Total : 0.f,
		FoamStart, FoamRange);
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
	bIslandsPushed = true;
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
	if (Player && !Player->IsSunk())
	{
		Ships.Add(Player);
	}

	TArray<AShipPawn*> Others;
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		AShipPawn* S = *It;
		if (IsValid(S) && S != Player && !S->IsSunk())
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
	for (FWakeTrail& T : Trails)
	{
		if (!T.Ship.IsValid() || !Ships.Contains(Cast<AShipPawn>(T.Ship.Get())))
		{
			T.Crumbs.Reset();
			T.Ages.Reset();
			T.bHasDropped = false;
			T.Ship = nullptr;
		}
	}

	for (AShipPawn* S : Ships)
	{
		// The slot this ship already owns, or else the first free one. Ships is
		// capped at MaxWakeShips and every slot not owned by a ship in it was
		// just freed above, so a free one always exists; the guard is there
		// because "always" is what the last version of this said too.
		FWakeTrail* Found = Trails.FindByPredicate(
			[S](const FWakeTrail& C) { return C.Ship.Get() == S; });
		if (!Found)
		{
			Found = Trails.FindByPredicate(
				[](const FWakeTrail& C) { return C.Ship.Get() == nullptr; });
			if (!Found)
			{
				continue;
			}
			Found->Ship = S;
			Found->Crumbs.Reset();
			Found->Ages.Reset();
			Found->bHasDropped = false;
		}
		FWakeTrail& T = *Found;

		for (float& A : T.Ages)
		{
			A += DeltaSeconds;
		}
		while (T.Ages.Num() > 0 && T.Ages.Last() > WakeLifeSeconds)
		{
			T.Ages.Pop();
			T.Crumbs.Pop();
		}

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
			while (T.Crumbs.Num() > CrumbsPerShip)
			{
				T.Crumbs.Pop();
				T.Ages.Pop();
			}
		}
	}

	// Two things that must be zero, counted rather than trusted, because the
	// bug they describe is invisible in every other number the wake prints:
	// the frozen trail was live, well-formed and the right length. A stranded
	// slot holds crumbs nobody is updating; a doubled ship owns two slots.
	int32 Stranded = 0, Doubled = 0;
	for (int32 a = 0; a < Trails.Num(); ++a)
	{
		if (Trails[a].Ship.Get() == nullptr && Trails[a].Crumbs.Num() > 0)
		{
			++Stranded;
		}
		for (int32 b = a + 1; b < Trails.Num(); ++b)
		{
			if (Trails[a].Ship.Get() != nullptr
				&& Trails[a].Ship.Get() == Trails[b].Ship.Get())
			{
				++Doubled;
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
	for (int32 s = 0; s < MaxWakeShips; ++s)
	{
		FLinearColor A(0.f, 0.f, 0.f, 0.f);
		FLinearColor B(0.f, 0.f, 0.f, 0.f);
		if (Ships.IsValidIndex(s) && IsValid(Ships[s]))
		{
			const FVector Where = Ships[s]->GetActorLocation();
			FVector Track = Ships[s]->GetVelocity();
			Track.Z = 0.f;
			// Her TRACK, not her heading: she makes leeway, and the water
			// closes behind where she actually went. Falls back to the heading
			// when she is barely moving, so the collar does not spin.
			const float SpeedCmS = Track.Size();
			const FVector Dir = (SpeedCmS > 1.f)
				? Track / SpeedCmS
				: Ships[s]->GetActorForwardVector().GetSafeNormal2D();
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
		UE_LOG(LogTemp, Display,
			TEXT("WAKELOG live=%d of %d tracked=%s ignored=%d stranded=%d doubled=%d "
				 "splashes=%d live=%d seen=%d lost=%d"),
			Live, WakePointCount, *Names, Ignored, Stranded, Doubled,
			MaxSplashes, LiveSplashes, SplashesSeen, SplashesOverwritten);
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
