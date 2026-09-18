#include "ShotTrail.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** -ShotTrails=0 turns them off. Read once and cached: FParse on every ball
	 *  on every tick is a string search per shot per frame. */
	bool TrailsWanted()
	{
		static bool bResolved = false;
		static bool bOn = true;
		if (!bResolved)
		{
			bResolved = true;
			int32 Want = 1;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShotTrails="), Want))
			{
				bOn = Want != 0;
			}
		}
		return bOn;
	}
}

AShotTrail::AShotTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	// AFTER the balls have moved. A ribbon built from last frame's positions
	// lags the shot by a frame, which at 300 m/s is three metres of visible gap
	// between the head of the trail and the ball it belongs to.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Ribbon = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ribbon"));
	SetRootComponent(Ribbon);
	Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ribbon->SetGenerateOverlapEvents(false);
	Ribbon->SetCastShadow(false);
	Ribbon->SetMobility(EComponentMobility::Movable);
	Ribbon->bUseAsyncCooking = false;
	// A wisp is not a surface: it has no business in the distance field, in
	// Lumen's scene, or in a reflection, where it would be read as an opaque
	// occluder with no base colour - a black smear that no colour can brighten.
	Ribbon->bAffectDistanceFieldLighting = false;
	Ribbon->bAffectDynamicIndirectLighting = false;
	Ribbon->SetVisibleInRayTracing(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrailMat(
		TEXT("/Game/Materials/M_ShotTrail.M_ShotTrail"));
	if (TrailMat.Succeeded())
	{
		Ribbon->SetMaterial(0, TrailMat.Object);
	}
}

void AShotTrail::BeginPlay()
{
	Super::BeginPlay();

	// -TrailTint=r,g,b, -TrailBright=x and -TrailOpacity=a, so the look is a
	// one-variable knob that needs neither a rebuild nor a material build
	// between probes. Every colour experiment before this one changed two things
	// at once - the material's default AND the C++ default - which is exactly how
	// a pair of runs stops proving anything.
	{
		FString TintList;
		if (FParse::Value(FCommandLine::Get(), TEXT("TrailTint="), TintList, false))
		{
			TArray<FString> Parts;
			TintList.ParseIntoArray(Parts, TEXT(","), true);
			if (Parts.Num() >= 3)
			{
				Tint = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]), 1.f);
			}
		}
		float Bright = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("TrailBright="), Bright) && Bright >= 0.f)
		{
			Tint = FLinearColor(Tint.R * Bright, Tint.G * Bright, Tint.B * Bright, 1.f);
		}
		float Solid = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("TrailOpacity="), Solid) && Solid >= 0.f)
		{
			Opacity = Solid;
		}
		FParse::Value(FCommandLine::Get(), TEXT("TrailHead="), HeadHalfCm);
		FParse::Value(FCommandLine::Get(), TEXT("TrailTail="), TailHalfCm);
	}

	// -TrailMat= swaps the material without a material build, which is how the
	// instanced-card version was finally cornered: a material proved to render
	// elsewhere drew nothing at all through that component.
	FString MatPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("TrailMat="), MatPath) && Ribbon)
	{
		if (MatPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			Ribbon->SetMaterial(0, nullptr);
		}
		else if (UMaterialInterface* Swap = LoadObject<UMaterialInterface>(nullptr, *MatPath))
		{
			Ribbon->SetMaterial(0, Swap);
		}
		UE_LOG(LogTemp, Display, TEXT("TRAILLOG material overridden to %s"), *MatPath);
	}

	TrailMaterial = Ribbon ? Ribbon->CreateAndSetMaterialInstanceDynamic(0) : nullptr;

	// Set, then READ BACK, and KEEP THE BOOL. The previous version discarded the
	// bool from GetVectorParameterValue and printed the value it had just stored,
	// which is the material instance echoing its own override: that line would
	// have read the same if the parameter had never existed. A readback that
	// cannot fail is not a readback.
	float OpacityBack = -1.f;
	FLinearColor ColourBack = FLinearColor::Black;
	bool bHasOpacity = false, bHasColour = false;
	if (TrailMaterial)
	{
		TrailMaterial->SetScalarParameterValue(TEXT("TrailOpacity"), Opacity);
		TrailMaterial->SetVectorParameterValue(TEXT("TrailColor"), Tint);
		bHasOpacity = TrailMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("TrailOpacity")), OpacityBack);
		bHasColour = TrailMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("TrailColor")), ColourBack);
	}

	UE_LOG(LogTemp, Display,
		TEXT("TRAILLOG ready: %s, life %.1f s, sample every %.0f cm, cone %.0f->%.0f cm, ")
		TEXT("cap %d/chain, mat=%s opacity=%.2f(found=%d) colour=(%.2f,%.2f,%.2f)(found=%d)"),
		TrailsWanted() ? TEXT("ON") : TEXT("off (-ShotTrails=0)"),
		LifeSeconds, SpacingCm, HeadHalfCm, TailHalfCm, MaxSamples,
		TrailMaterial ? TEXT("dynamic") : TEXT("NONE"),
		OpacityBack, bHasOpacity ? 1 : 0,
		ColourBack.R, ColourBack.G, ColourBack.B, bHasColour ? 1 : 0);

	if (TrailMaterial && (!bHasOpacity || !bHasColour))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("TRAILLOG the material is MISSING a parameter this actor drives ")
			TEXT("(opacity=%d colour=%d) - the knob is not connected to anything"),
			bHasOpacity ? 1 : 0, bHasColour ? 1 : 0);
	}
}

AShotTrail* AShotTrail::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AShotTrail> It(World); It; ++It)
	{
		return *It;
	}
	return World->SpawnActor<AShotTrail>(AShotTrail::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator);
}

int32 AShotTrail::OpenChain()
{
	for (int32 i = 0; i < Chains.Num(); ++i)
	{
		if (!Chains[i].bInUse)
		{
			Chains[i] = FChain();
			Chains[i].bInUse = true;
			Chains[i].bOpen = true;
			return i;
		}
	}
	FChain Fresh;
	Fresh.bInUse = true;
	Fresh.bOpen = true;
	return Chains.Add(MoveTemp(Fresh));
}

bool AShotTrail::Lay(UWorld* World, const FVector& Where, int32& ChainId)
{
	if (!TrailsWanted())
	{
		return false;
	}
	AShotTrail* Trail = Find(World);
	if (!Trail)
	{
		return false;
	}
	if (!Trail->Chains.IsValidIndex(ChainId) || !Trail->Chains[ChainId].bOpen)
	{
		ChainId = Trail->OpenChain();
		Trail->Add(ChainId, Where);
		return true;
	}
	// BY DISTANCE, not by tick. Tying the sampling to the frame would make the
	// same shot on the same seed draw a different ribbon at a different frame
	// rate, and this project compares runs for a living.
	const FChain& Chain = Trail->Chains[ChainId];
	if (FVector::DistSquared(Chain.LastAt, Where)
		< Trail->SpacingCm * Trail->SpacingCm)
	{
		return true;
	}
	Trail->Add(ChainId, Where);
	return true;
}

void AShotTrail::Close(UWorld* World, int32 ChainId, const FVector& Where)
{
	if (!TrailsWanted() || ChainId == INDEX_NONE)
	{
		return;
	}
	AShotTrail* Trail = Find(World);
	if (!Trail || !Trail->Chains.IsValidIndex(ChainId))
	{
		return;
	}
	// The last sample goes in WITHOUT the spacing test. The ball may have died
	// two metres past her last sample, and a ribbon that stops two metres short
	// of the splash is a ribbon that does not answer the one question it exists
	// for: where did the shot actually land.
	Trail->Add(ChainId, Where);
	Trail->Chains[ChainId].bOpen = false;
}

void AShotTrail::Add(int32 ChainId, const FVector& Where)
{
	if (!Chains.IsValidIndex(ChainId))
	{
		return;
	}
	FChain& Chain = Chains[ChainId];
	if (Chain.Points.Num() >= FMath::Max(2, MaxSamples))
	{
		// The cap bit. Counted rather than silent: an ordinary action should
		// never reach it, and if it does the number says so instead of the
		// oldest part of an arc quietly disappearing.
		Chain.Points.RemoveAt(0);
		++Discarded;
		--Live;
	}
	FSample S;
	S.Where = Where;
	S.Age = 0.f;
	Chain.Points.Add(S);
	Chain.LastAt = Where;
	++Live;
	++Laid;
}

void AShotTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Age first, and drop from the FRONT: a chain holds its samples oldest
	// first, so the ribbon dies from the tail forwards, which is what smoke
	// does. Dropping from the back would eat the head and the trail would
	// detach from the ball it belongs to.
	int32 Expired = 0;
	LiveChains = 0;
	for (FChain& Chain : Chains)
	{
		if (!Chain.bInUse)
		{
			continue;
		}
		int32 Cut = 0;
		for (FSample& S : Chain.Points)
		{
			S.Age += DeltaSeconds;
			if (S.Age >= LifeSeconds)
			{
				++Cut;
			}
			else
			{
				break;   // ordered by age, so the first survivor ends it
			}
		}
		if (Cut > 0)
		{
			Chain.Points.RemoveAt(0, Cut);
			Live -= Cut;
			Expired += Cut;
		}
		if (Chain.Points.Num() == 0 && !Chain.bOpen)
		{
			Chain.bInUse = false;
		}
		else if (Chain.Points.Num() > 0)
		{
			++LiveChains;
		}
	}

	// The camera, for turning the ribbon's flat face to the viewer. Taken from
	// the camera manager and not from any pawn: a ribbon twisted to face the
	// SHIP instead of the eye shows edge-on from abeam, which is a hairline.
	FVector CamLoc = FVector::ZeroVector;
	if (const UWorld* W = GetWorld())
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
			{
				CamLoc = Cam->GetCameraLocation();
			}
		}
	}

	Rebuild(CamLoc);

	LogTimer += DeltaSeconds;
	if (LogTimer >= 2.f)
	{
		LogTimer = 0.f;
		UE_LOG(LogTemp, Display,
			TEXT("TRAILLOG live=%d chains=%d laid=%d discarded=%d stranded=%d expired=%d"),
			Live, LiveChains, Laid, Discarded, Stranded, Expired);
	}
}

void AShotTrail::Rebuild(const FVector& CamLoc)
{
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colours;
	TArray<FProcMeshTangent> Tangents;

	int32 Drawn = 0;
	for (const FChain& Chain : Chains)
	{
		const int32 N = Chain.Points.Num();
		if (!Chain.bInUse || N < 2)
		{
			// A chain of one point has no direction, so it has no ribbon. Its
			// sample is still alive and still counted; it just cannot be drawn
			// until the ball has moved far enough to give it a tangent.
			continue;
		}
		const int32 Base = Verts.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FSample& S = Chain.Points[i];
			++Drawn;

			// The tangent: central difference in the middle, one-sided at the
			// ends. This is what makes the ribbon follow the ballistic CURVE
			// rather than kink at every sample.
			FVector D;
			if (i == 0)
			{
				D = Chain.Points[1].Where - S.Where;
			}
			else if (i == N - 1)
			{
				D = S.Where - Chain.Points[i - 1].Where;
			}
			else
			{
				D = Chain.Points[i + 1].Where - Chain.Points[i - 1].Where;
			}
			D = D.GetSafeNormal();

			FVector ToEye = (CamLoc - S.Where).GetSafeNormal();
			FVector Side = FVector::CrossProduct(D, ToEye);
			if (Side.SizeSquared() < KINDA_SMALL_NUMBER)
			{
				// The shot is flying straight at or away from the camera, so
				// there is no in-view side to widen along. Any perpendicular
				// will do and none of it is visible anyway; what matters is
				// that it is never a zero vector, which would collapse the
				// ribbon into a line of degenerate triangles.
				Side = FVector::CrossProduct(D, FVector::UpVector);
				if (Side.SizeSquared() < KINDA_SMALL_NUMBER)
				{
					Side = FVector::CrossProduct(D, FVector::ForwardVector);
				}
			}
			Side = Side.GetSafeNormal();

			// THE CONE. Age drives the width: the head is where the smoke has
			// just left the ball, the tail is four seconds of spreading later.
			const float Age01 = FMath::Clamp(S.Age / LifeSeconds, 0.f, 1.f);
			float Half = FMath::Lerp(HeadHalfCm, TailHalfCm, Age01);

			// And the two ends come to a point instead of being cut off square.
			// The head especially: a blunt end on the newest sample reads as a
			// flag on a stick rather than as smoke coming off a moving object.
			if (i == N - 1)
			{
				Half *= 0.18f;
			}
			else if (i == N - 2 && N > 2)
			{
				Half *= 0.55f;
			}
			if (i == 0)
			{
				Half *= 0.65f;
			}

			// Thinning as it dies, carried on the vertex colour so one material
			// serves every sample of every chain at once - which is the thing
			// an instanced card could not do without per-instance data.
			const float Fade = FMath::Pow(1.f - Age01, 1.4f);

			const FVector Out = Side * Half;
			Verts.Add(S.Where - Out);
			Verts.Add(S.Where + Out);
			const FVector Facing = FVector::CrossProduct(Side, D).GetSafeNormal();
			Normals.Add(Facing);
			Normals.Add(Facing);
			const float U = N > 1 ? (float)i / (float)(N - 1) : 0.f;
			UVs.Add(FVector2D(U, 0.f));
			UVs.Add(FVector2D(U, 1.f));
			Colours.Add(FLinearColor(1.f, 1.f, 1.f, Fade));
			Colours.Add(FLinearColor(1.f, 1.f, 1.f, Fade));
			Tangents.Add(FProcMeshTangent(D, false));
			Tangents.Add(FProcMeshTangent(D, false));
		}
		for (int32 i = 0; i < N - 1; ++i)
		{
			const int32 A = Base + i * 2;
			Tris.Add(A);     Tris.Add(A + 1); Tris.Add(A + 2);
			Tris.Add(A + 1); Tris.Add(A + 3); Tris.Add(A + 2);
		}
	}

	// A sample in the mesh that should already have expired is the defect the
	// wake shipped once: crumbs nobody advanced, invisible because the counter
	// that would have caught them was sampled per frame instead of latched.
	// This one latches, and it counts against LIVE rather than against the
	// vertex total, because a chain of one point is alive and undrawable.
	if (Drawn > Live)
	{
		Stranded += Drawn - Live;
	}

	if (!Ribbon)
	{
		return;
	}
	if (Verts.Num() == 0)
	{
		Ribbon->ClearMeshSection(0);
		return;
	}
	// Rebuilt whole every frame rather than updated in place: the vertex count
	// changes on any frame a sample is taken or expires, and UpdateMeshSection
	// cannot change a count. A few hundred vertices is nothing.
	Ribbon->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colours,
		Tangents, /*bCreateCollision*/ false);
}
