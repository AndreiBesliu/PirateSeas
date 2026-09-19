#include "GunSmoke.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "WindSubsystem.h"

TArray<TWeakObjectPtr<AGunSmoke>> AGunSmoke::Live;
int32 AGunSmoke::Spawned = 0;
int32 AGunSmoke::Culled = 0;
int32 AGunSmoke::Stranded = 0;

int32 AGunSmoke::CountLive()
{
	Live.RemoveAll([](const TWeakObjectPtr<AGunSmoke>& P) { return !P.IsValid(); });
	return Live.Num();
}

void AGunSmoke::ResetForNewLevel()
{
	Live.Reset();
	Spawned = 0;
	Culled = 0;
	Stranded = 0;
}

AGunSmoke::AGunSmoke()
{
	PrimaryActorTick.bCanEverTick = true;

	Cards = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cards"));
	RootComponent = Cards;
	Cards->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cards->SetGenerateOverlapEvents(false);
	Cards->SetCastShadow(false);
	Cards->bReceivesDecals = false;
	Cards->SetMobility(EComponentMobility::Movable);
	// The cards are repositioned every frame; letting the engine rebuild a
	// bounds tree for eighteen quads sixty times a second is pure cost.
	Cards->SetCullDistance(0);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded())
	{
		Cards->SetStaticMesh(Plane.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeMat(
		TEXT("/Game/Materials/M_GunSmoke.M_GunSmoke"));
	if (SmokeMat.Succeeded())
	{
		Cards->SetMaterial(0, SmokeMat.Object);
	}
}

AGunSmoke* AGunSmoke::Spawn(UWorld* World, const FVector& Muzzle,
	const FVector& JetDir, int32 Seed)
{
	if (!World)
	{
		return nullptr;
	}

	// Cull the oldest before adding, and SAY SO. A cap that trims quietly reads
	// afterwards as "there was never more than this many".
	Live.RemoveAll([](const TWeakObjectPtr<AGunSmoke>& P) { return !P.IsValid(); });
	if (Live.Num() >= MaxLivePuffs)
	{
		// CulledHere, not Culled: the member of that name now accumulates over
		// the whole run, and a local shadowing it would have left the quit line
		// reporting only the last cull - a number that looks like a total.
		int32 CulledHere = 0;
		while (Live.Num() >= MaxLivePuffs && Live.Num() > 0)
		{
			if (AGunSmoke* Old = Live[0].Get())
			{
				Old->Destroy();
			}
			Live.RemoveAt(0);
			++CulledHere;
			++Culled;   // and over the whole run, for the quit line
		}
		UE_LOG(LogTemp, Display,
			TEXT("SMOKELOG culled %d oldest puffs at the %d cap"), CulledHere, MaxLivePuffs);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGunSmoke* Puff = World->SpawnActorDeferred<AGunSmoke>(
		AGunSmoke::StaticClass(), FTransform(Muzzle), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Puff)
	{
		return nullptr;
	}

	Puff->Centre = Muzzle;
	Puff->BirthZ = Muzzle.Z;

	// Its OWN stream, seeded from the shot index. The gun scatter runs on the
	// one global stream that -ShipSeed pins (SeaGameMode seeds it with
	// FMath::RandInit), and drawing eighteen numbers per gun off that stream
	// would shift every subsequent shot - smoke would change where the balls go.
	FRandomStream Stream(Seed * 7919 + 13);
	const FVector Jet = JetDir.GetSafeNormal();
	Puff->Deck.Reserve(Puff->CardCount);
	for (int32 i = 0; i < Puff->CardCount; ++i)
	{
		FCard C;
		// A little cone about the jet, so the cards do not leave in one line.
		const FVector Scatter(Stream.FRandRange(-0.22f, 0.22f),
			Stream.FRandRange(-0.22f, 0.22f), Stream.FRandRange(-0.12f, 0.22f));
		const float Speed = FMath::Lerp(Puff->JetSpeedMinCmS, Puff->JetSpeedMaxCmS,
			Stream.FRand());
		C.Velocity = (Jet + Scatter).GetSafeNormal() * Speed;

		// Seeded through a BALL, not at a point. Cards born coincident stay
		// coincident - the spread term below pushes each one along its own
		// offset, and an offset of almost zero has no direction to be pushed
		// along. The first version scattered them by forty centimetres and then
		// wondered why the cloud was flat.
		FVector Dir(Stream.FRandRange(-1.f, 1.f), Stream.FRandRange(-1.f, 1.f),
			Stream.FRandRange(-1.f, 1.f));
		if (Dir.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			Dir = FVector::UpVector;
		}
		// Cube root, so the cards fill the ball evenly instead of crowding the rim.
		C.Offset = Dir.GetSafeNormal() * Puff->BirthRadiusCm
			* FMath::Pow(Stream.FRand(), 1.f / 3.f);
		C.Spin = FMath::DegreesToRadians(
			Stream.FRandRange(-Puff->SpinDegPerSecond, Puff->SpinDegPerSecond));
		C.Roll = Stream.FRandRange(0.f, 2.f * PI);
		C.Size = Puff->CardStartCm * Stream.FRandRange(0.65f, 1.45f);
		Puff->Deck.Add(C);
	}

	Puff->FinishSpawning(FTransform(Muzzle));
	Live.Add(Puff);
	++Spawned;
	return Puff;
}

void AGunSmoke::BeginPlay()
{
	Super::BeginPlay();

	SmokeMaterial = Cards ? Cards->CreateAndSetMaterialInstanceDynamic(0) : nullptr;

	// The wind at birth, held for the life of the puff. Smoke does not re-steer
	// itself when the breeze shifts; it goes where it was set going.
	if (const UWindSubsystem* Wind = GetWorld()
		? GetWorld()->GetSubsystem<UWindSubsystem>() : nullptr)
	{
		DriftCmS = Wind->GetWindDirection() * Wind->GetWindSpeedMS() * 100.f;
	}

	// Which way the sun is, read off the level rather than guessed, and logged
	// so a wrong-looking rim can be told from a wrong-looking number.
	FVector SunDir(0.4f, 0.75f, 0.53f);
	if (GetWorld())
	{
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			SunDir = -It->GetActorForwardVector();
			break;
		}
	}
	if (SmokeMaterial)
	{
		SmokeMaterial->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir));
	}

	if (Live.Num() <= 1)
	{
		// Every assumption this effect rests on, stated out loud once. The
		// debug build showed flat grey where the noise should have been, which
		// says the SAMPLE is wrong - and a sample can be wrong because the
		// texture is wrong, or because the mesh carries no UVs to sample it
		// with, or because nothing got a transform and eighteen cards are
		// stacked in one place. Those are different bugs with one symptom.
		const UStaticMesh* Mesh = Cards ? Cards->GetStaticMesh() : nullptr;
		int32 UVs = -1;
		if (Mesh && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
		{
			UVs = Mesh->GetRenderData()->LODResources[0]
				.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		}
		UE_LOG(LogTemp, Display,
			TEXT("SMOKELOG first puff: cards=%d instances=%d mesh=%s uvChannels=%d mid=%s life=%.1fs drift=%.0f cm/s sun=(%.2f,%.2f,%.2f)"),
			Deck.Num(), Cards ? Cards->GetInstanceCount() : -1,
			Mesh ? *Mesh->GetName() : TEXT("NONE"), UVs,
			SmokeMaterial ? *SmokeMaterial->GetName() : TEXT("NONE"),
			LifeSeconds, DriftCmS.Size2D(), SunDir.X, SunDir.Y, SunDir.Z);
	}

	if (Cards)
	{
		Cards->ClearInstances();
		for (int32 i = 0; i < Deck.Num(); ++i)
		{
			Cards->AddInstance(FTransform::Identity, /*bWorldSpace=*/false);
		}
	}
}

void AGunSmoke::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= LifeSeconds)
	{
		Destroy();
		return;
	}

	// MUST NEVER FIRE. Directly after the guard above, so the only way to reach
	// it is to weaken that guard - which is exactly how this counter is proven
	// to work, rather than being a zero nobody can move. Counted per tick, not
	// latched per puff: for a "never" counter the more sensitive form is the
	// right one.
	if (Age >= LifeSeconds)
	{
		++Stranded;
	}

	const float Age01 = Age / LifeSeconds;

	// The wind takes hold gradually: at full strength from the first frame the
	// puff is gone downwind before it has formed.
	const float Ramp = FMath::Lerp(DriftStartFraction, 1.f,
		FMath::Clamp(Age / FMath::Max(0.01f, DriftRampSeconds), 0.f, 1.f));

	// The camera, for billboarding. Taken from the camera manager DIRECTLY and
	// not from AOceanSurface::GetFocus, which returns the PAWN first - facing
	// the cards at the ship instead of at the viewer, which from abeam is
	// ninety degrees wrong and shows them edge-on.
	FVector CamLoc = GetActorLocation() + FVector(0.f, 0.f, 1000.f);
	if (const APlayerController* PC = GetWorld()
		? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		}
	}

	Centre += DriftCmS * Ramp * DeltaSeconds;
	Centre.Z += RiseCmS * DeltaSeconds;

	TArray<FTransform> Xforms;
	Xforms.Reserve(Deck.Num());
	for (FCard& C : Deck)
	{
		// Jet, then drag. Fourteen to thirty metres a second decaying under two
		// inside four tenths: the smoke stands off the side and stalls.
		if (Age < JetSeconds)
		{
			C.Offset += C.Velocity * DeltaSeconds;
			C.Velocity *= FMath::Exp(-JetDragPerSecond * DeltaSeconds);
		}

		// The cloud swells outward from its own centre.
		const FVector Out = C.Offset.GetSafeNormal();
		C.Offset += Out * SpreadCmS * DeltaSeconds;
		C.Size += CardGrowthCmS * DeltaSeconds;
		C.Roll += C.Spin * DeltaSeconds;

		FVector World = Centre + C.Offset;
		// Shear: the higher a card has climbed, the further the wind has carried
		// it. A column that leans is a column; one that does not is a balloon.
		const float GainedM = FMath::Max(0.f, World.Z - BirthZ) * 0.01f;
		World += DriftCmS * Ramp * ShearPerMetre * GainedM * Age;

		const FVector ToCam = (CamLoc - World).GetSafeNormal();
		const FQuat Face = FRotationMatrix::MakeFromZ(ToCam).ToQuat();
		const FQuat Spun = Face * FQuat(FVector::UpVector, C.Roll);
		const float S = C.Size / 100.f;    // the engine plane is 100 cm square
		Xforms.Add(FTransform(Spun, World - GetActorLocation(), FVector(S, S, S)));
	}

	if (Cards && Xforms.Num() == Cards->GetInstanceCount())
	{
		Cards->BatchUpdateInstancesTransforms(0, Xforms, false, true, false);
		// Counted, not assumed: if the cards never move, eighteen of them sit in
		// one place and the puff is one card seen eighteen times over.
		if (!bReportedSpread && Age > 1.f)
		{
			bReportedSpread = true;
			FBox Span(ForceInit);
			for (const FTransform& X : Xforms)
			{
				Span += X.GetLocation();
			}
			UE_LOG(LogTemp, Display,
				TEXT("SMOKELOG at t=%.1f the cards span %.0f x %.0f x %.0f cm, sizes %.0f..%.0f"),
				Age, Span.GetSize().X, Span.GetSize().Y, Span.GetSize().Z,
				Deck[0].Size, Deck.Last().Size);
		}
	}
	else if (Cards)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SMOKELOG transform count %d != instance count %d - cards are NOT being moved"),
			Xforms.Num(), Cards->GetInstanceCount());
	}

	if (SmokeMaterial)
	{
		SmokeMaterial->SetScalarParameterValue(TEXT("Age01"), Age01);
		SmokeMaterial->SetScalarParameterValue(TEXT("SmokeTime"), Age);
		SmokeMaterial->SetVectorParameterValue(TEXT("PuffCentre"),
			FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
	}
}
