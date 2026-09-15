#include "ShipHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "SeaGameMode.h"
#include "ShipPawn.h"
#include "WindSubsystem.h"

namespace
{
	const FLinearColor Ink(0.92f, 0.93f, 0.90f, 0.92f);
	const FLinearColor Faint(0.92f, 0.93f, 0.90f, 0.30f);
	const FLinearColor Panel(0.02f, 0.04f, 0.06f, 0.55f);
	const FLinearColor Good(0.55f, 0.80f, 0.55f, 0.95f);
	const FLinearColor Warn(0.95f, 0.72f, 0.28f, 0.95f);
	const FLinearColor Bad(0.90f, 0.33f, 0.27f, 0.95f);
	const FLinearColor Canvas0(0.55f, 0.72f, 0.90f, 0.85f);
	const FLinearColor DeadWater(0.90f, 0.33f, 0.27f, 0.13f);
	const FLinearColor Ghost(0.55f, 0.72f, 0.90f, 0.22f);
}

AShipHUD::AShipHUD()
{
	// AHUD asks to tick and this class needs it: the panel's log lives in
	// Tick, because DrawHUD does not run at all under -NullRHI.
	PrimaryActorTick.bCanEverTick = true;
}

void AShipHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	LogTimer += DeltaSeconds;
	if (LogTimer < 1.f)
	{
		return;
	}
	LogTimer = 0.f;

	AShipPawn* Ship = GetShip();
	if (!Ship)
	{
		UE_LOG(LogTemp, Display, TEXT("HUDLOG no ship"));
		return;
	}
	UWindSubsystem* Wind = GetWorld() ? GetWorld()->GetSubsystem<UWindSubsystem>() : nullptr;

	UE_LOG(LogTemp, Display,
		TEXT("HUDLOG windRel=%.0f windAng=%.0f trackAng=%.0f trim=%.2f speed=%.2f vmg=%.2f leeway=%.1f hull=%.0f rig=%.2f/%.2f rud=%.2f guns=%d/%d reload=%.0f/%.0f aim=%s hands=%d/%d repair=%d"),
		Wind ? FMath::FindDeltaAngleDegrees(Ship->GetActorRotation().Yaw,
			FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f)) : 0.f,
		Ship->GetWindAngleDeg(), Ship->GetTrackWindAngleDeg(),
		Ship->GetSailTrim(), Ship->GetForwardSpeedMS(), Ship->GetWindwardVMG(),
		Ship->GetLeewayDeg(), Ship->GetHullIntegrity(), Ship->GetForeRigIntegrity(),
		Ship->GetMainRigIntegrity(), Ship->GetRudderIntegrity(),
		Ship->GetGunsRemaining(false), Ship->GetGunsRemaining(true),
		Ship->GetReloadRemaining(false), Ship->GetReloadRemaining(true),
		Ship->IsAimingHigh() ? TEXT("high") : TEXT("low"),
		Ship->GetHands(), Ship->GetHandsMax(), Ship->GetHandsOnRepair());
}

AShipPawn* AShipHUD::GetShip() const
{
	return PlayerOwner ? Cast<AShipPawn>(PlayerOwner->GetPawn()) : nullptr;
}

void AShipHUD::DrawDisc(const FVector2D& Centre, float Radius, const FLinearColor& Colour) const
{
	// Scanlines, because there is no circle primitive. DrawRect fills;
	// K2_DrawBox does NOT, it draws four lines, and the first version of this
	// spent thirteen hundred line primitives a frame faking a fill with
	// overlapping translucent outlines.
	const int32 Steps = FMath::Max(10, FMath::RoundToInt(Radius / 1.5f));
	for (int32 i = -Steps; i <= Steps; ++i)
	{
		const float Y = Radius * i / Steps;
		const float Half = FMath::Sqrt(FMath::Max(0.f, Radius * Radius - Y * Y));
		const_cast<AShipHUD*>(this)->DrawRect(Colour, Centre.X - Half,
			Centre.Y + Y, Half * 2.f, Radius / Steps + 1.f);
	}
}

FVector2D AShipHUD::RosePoint(float RelativeDeg, float Radius) const
{
	// Bow up, starboard right: a bearing measured clockwise from the bow.
	const float R = FMath::DegreesToRadians(RelativeDeg);
	return FVector2D(RoseCentre.X + Radius * FMath::Sin(R),
		RoseCentre.Y - Radius * FMath::Cos(R));
}

void AShipHUD::DrawHUD()
{
	// The guard comes FIRST: AHUD::DrawHUD ends by dereferencing Canvas, so a
	// check placed after the Super call can never run.
	if (!Canvas || Canvas->SizeY <= 0)
	{
		return;
	}
	Super::DrawHUD();

	// Everything is sized from the viewport height, so the panel keeps its
	// proportions whatever the window is. 1080 is simply the height the
	// numbers below were chosen against.
	Scale = FMath::Max(0.55f, Canvas->SizeY / 1080.f);
	Line = 22.f * Scale;
	Big = GEngine->GetMediumFont();
	Small = GEngine->GetSmallFont();

	RoseCentre = FVector2D(Canvas->SizeX * RoseCentreFraction.X,
		Canvas->SizeY * RoseCentreFraction.Y);
	RoseRadius = Canvas->SizeY * RoseRadiusFraction;

	AShipPawn* Ship = GetShip();
	UWindSubsystem* Wind = GetWorld() ? GetWorld()->GetSubsystem<UWindSubsystem>() : nullptr;

	if (!Ship)
	{
		// Between a sinking and a new ship the controller holds nothing. Say
		// so rather than drawing a panel full of zeroes.
		const FString Text = TEXT("NO SHIP - WAITING FOR A NEW COMMAND");
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Big, Text, W, H, Scale, Scale);
		DrawText(Text, Warn, (Canvas->SizeX - W) * 0.5f, Canvas->SizeY * 0.5f, Big, Scale);
		return;
	}

	DrawRose(Ship, Wind);
	DrawCondition(Ship);
	DrawGuns(Ship);
	DrawWay(Ship);
	DrawWarnings(Ship, Wind);
}

void AShipHUD::DrawRose(AShipPawn* Ship, UWindSubsystem* Wind)
{
	const float Inner = RoseRadius * 0.30f;

	// The wind's bearing relative to the bow. Everything on the rose is drawn
	// relative to the ship, not to north: a helmsman steers by the wind.
	const float WindRel = Wind
		? FMath::FindDeltaAngleDegrees(Ship->GetActorRotation().Yaw,
			FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f))
		: 0.f;
	const float NoGo = Ship->GetNoGoAngleDeg();

	// A disc behind it, or the thin lines vanish against a bright sea.
	DrawDisc(RoseCentre, RoseRadius * 1.28f, FLinearColor(0.02f, 0.04f, 0.06f, 0.55f));

	// The dead sector, but only if the wind is actually known. Without it the
	// panel used to draw a confident red wedge straight across the bow, which
	// is an instrument inventing a reading rather than admitting it has none.
	if (Wind)
	{
		for (float A = -NoGo; A <= NoGo; A += 3.f)
		{
			const FVector2D P = RosePoint(WindRel + A, RoseRadius);
			DrawLine(RoseCentre.X, RoseCentre.Y, P.X, P.Y, DeadWater, 3.f * Scale);
		}
	}

	// The polar itself: for each heading she could turn to, how hard the rig
	// would pull if she were on it. This is the instrument. The bow is at the
	// top, so the radius straight up is the power she has right now.
	// Drawn twice: a ghost for what a whole rig would give, and a solid line
	// for what she has now. With the masts shot away the solid line collapses
	// into the hub and the gap between the two is the damage, in the same
	// picture that tells her where to steer.
	const float Rig = Ship->GetRigEfficiency();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bGhost = (Pass == 0);
		if (bGhost && Rig > 0.98f)
		{
			continue;   // nothing lost, nothing to mourn
		}
		FVector2D Prev = FVector2D::ZeroVector;
		for (int32 Step = 0; Step <= 72; ++Step)
		{
			const float Rel = Step * 5.f;
			const float WouldBe = FMath::Abs(FMath::FindDeltaAngleDegrees(Rel, WindRel));
			const float Drive = Ship->SailDriveCoefficient(WouldBe) * (bGhost ? 1.f : Rig);
			const FVector2D P = RosePoint(Rel, Inner + Drive * (RoseRadius - Inner));
			if (Step > 0)
			{
				DrawLine(Prev.X, Prev.Y, P.X, P.Y, bGhost ? Ghost : Canvas0,
					(bGhost ? 1.5f : 2.5f) * Scale);
			}
			Prev = P;
		}
	}

	// The ring, and the beam marks: a square rig fights on her broadside, so
	// where the beam points matters as much as where the bow does.
	for (int32 Step = 0; Step < 72; ++Step)
	{
		const FVector2D A = RosePoint(Step * 5.f, RoseRadius);
		const FVector2D B = RosePoint((Step + 1) * 5.f, RoseRadius);
		DrawLine(A.X, A.Y, B.X, B.Y, Faint, 1.f * Scale);
	}
	for (float Beam : { 90.f, 270.f })
	{
		const FVector2D A = RosePoint(Beam, RoseRadius * 0.88f);
		const FVector2D B = RosePoint(Beam, RoseRadius);
		DrawLine(A.X, A.Y, B.X, B.Y, Faint, 2.f * Scale);
	}

	// Where the wind comes from, as a barb pointing in at the ring.
	if (Wind)
	{
		const FVector2D Tip = RosePoint(WindRel, RoseRadius * 1.02f);
		const FVector2D Tail = RosePoint(WindRel, RoseRadius * 1.24f);
		DrawLine(Tail.X, Tail.Y, Tip.X, Tip.Y, Ink, 3.f * Scale);
		const FVector2D L = RosePoint(WindRel - 7.f, RoseRadius * 1.14f);
		const FVector2D R = RosePoint(WindRel + 7.f, RoseRadius * 1.14f);
		DrawLine(L.X, L.Y, Tip.X, Tip.Y, Ink, 3.f * Scale);
		DrawLine(R.X, R.Y, Tip.X, Tip.Y, Ink, 3.f * Scale);

		// Inside the ring, under the hub: outside it the label wandered off
		// the bottom of the screen whenever the wind came from astern.
		const FString WindText = FString::Printf(TEXT("WIND %.0f m/s  %.0f deg"),
			Wind->GetWindSpeedMS(), FMath::Abs(WindRel));
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Small, WindText, W, H, Scale, Scale);
		DrawText(WindText, Ink, RoseCentre.X - W * 0.5f,
			RoseCentre.Y + RoseRadius * 1.32f, Small, Scale);

		// The two headings that make the most ground to windward, one on each
		// tack. Steering to one of these is how you get anywhere upwind, and
		// it is never the heading that feels fastest.
		for (float Side : { -1.f, 1.f })
		{
			const FVector2D A = RosePoint(WindRel + Side * BestBeatAngleDeg, Inner);
			const FVector2D B = RosePoint(WindRel + Side * BestBeatAngleDeg, RoseRadius);
			DrawLine(A.X, A.Y, B.X, B.Y, Good, 1.5f * Scale);
		}
	}

	// The bow: a solid mark straight up, and the track she is actually making
	// once leeway is counted, which is never quite the same line.
	const bool bInIrons = Wind && Ship->GetWindAngleDeg() < NoGo;
	const FVector2D BowTip = RosePoint(0.f, RoseRadius * 0.97f);
	DrawLine(RoseCentre.X, RoseCentre.Y, BowTip.X, BowTip.Y,
		bInIrons ? Bad : Ink, 3.f * Scale);

	// Gated on the TRACK, the same quantity the pawn uses to decide whether
	// leeway is meaningful at all. Gating on forward speed hid the needle in
	// the one case that matters: a hull making almost pure sideslip.
	const float Leeway = Ship->GetLeewayDeg();
	if (Ship->GetVelocity().Size2D() > 50.f)
	{
		const FVector2D Track = RosePoint(Leeway, RoseRadius * 0.82f);
		DrawLine(RoseCentre.X, RoseCentre.Y, Track.X, Track.Y, Warn, 2.f * Scale);
	}
}

float AShipHUD::DrawBar(float X, float Y, float Width, const FString& Label,
	float Fraction, const FString& Value)
{
	const float H = Line * 0.55f;
	const float LabelWidth = Width * 0.34f;
	const float BarWidth = Width - LabelWidth;

	DrawText(Label, Faint, X, Y, Small, Scale);

	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.10f), X + LabelWidth, Y, BarWidth, H);
	const float F = FMath::Clamp(Fraction, 0.f, 1.f);
	// What has been lost, drawn as well as what is left. Without it a bar at
	// zero is an empty grey slot, pixel for pixel identical to a bar that was
	// never drawn, and total loss becomes the quietest reading on the panel.
	DrawRect(FLinearColor(0.55f, 0.12f, 0.10f, 0.55f),
		X + LabelWidth + BarWidth * F, Y, BarWidth * (1.f - F), H);
	const FLinearColor Colour = (F <= 0.02f) ? Bad : (F < WarnFraction ? Warn : Good);
	DrawRect(Colour, X + LabelWidth, Y, BarWidth * F, H);

	if (!Value.IsEmpty())
	{
		DrawText(Value, Ink, X + LabelWidth + BarWidth + 8.f * Scale, Y, Small, Scale);
	}
	return Y + Line * 0.82f;
}

void AShipHUD::DrawCondition(AShipPawn* Ship)
{
	const float X = Canvas->SizeX * 0.030f;
	const float Width = Canvas->SizeX * 0.185f;
	float Y = Canvas->SizeY * 0.735f;

	DrawRect(Panel, X - 12.f * Scale, Y - Line * 0.9f,
		Width + Canvas->SizeX * 0.050f, Line * 6.25f);
	DrawText(TEXT("CONDITION"), Faint, X, Y - Line * 0.75f, Small, Scale);

	Y = DrawBar(X, Y, Width, TEXT("HULL"),
		Ship->GetHullIntegrity() / FMath::Max(1.f, Ship->GetMaxHullIntegrity()),
		FString::Printf(TEXT("%.0f"), Ship->GetHullIntegrity()));
	Y = DrawBar(X, Y, Width, TEXT("FORE"), Ship->GetForeRigIntegrity(), FString());
	Y = DrawBar(X, Y, Width, TEXT("MAIN"), Ship->GetMainRigIntegrity(), FString());
	Y = DrawBar(X, Y, Width, TEXT("RUDDER"), Ship->GetRudderIntegrity(), FString());
	Y = DrawBar(X, Y, Width, TEXT("SAIL SET"), Ship->GetSailTrim(),
		FString::Printf(TEXT("%.0f%%"), Ship->GetSailTrim() * 100.f));
	// The men, and where they are. R moves a quarter of them at a time.
	const int32 OnRepair = Ship->GetHandsOnRepair();
	Y = DrawBar(X, Y, Width, TEXT("HANDS"),
		Ship->GetHands() / FMath::Max(1.f, (float)Ship->GetHandsMax()),
		OnRepair > 0
			? FString::Printf(TEXT("%d/%d  %d repairing (R)"), Ship->GetHands(), Ship->GetHandsMax(), OnRepair)
			: FString::Printf(TEXT("%d/%d  all at the guns (R)"), Ship->GetHands(), Ship->GetHandsMax()));
}

void AShipHUD::DrawGuns(AShipPawn* Ship)
{
	const float X = Canvas->SizeX * 0.775f;
	const float Width = Canvas->SizeX * 0.170f;
	float Y = Canvas->SizeY * 0.775f;

	DrawRect(Panel, X - 12.f * Scale, Y - Line * 0.9f, Width + 24.f * Scale, Line * 3.4f);
	DrawText(Ship->IsAimingHigh() ? TEXT("GUNS - POINTED HIGH") : TEXT("GUNS"),
		Ship->IsAimingHigh() ? Warn : Faint, X, Y - Line * 0.75f, Small, Scale);

	const float Reload = FMath::Max(1.f, Ship->GetReloadSeconds());
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bStarboard = (Side == 1);
		const int32 Mounted = Ship->GetGunsRemaining(bStarboard);
		const float Left = Ship->GetReloadRemaining(bStarboard);

		DrawText(bStarboard ? TEXT("E  STBD") : TEXT("Q  PORT"), Faint, X, Y, Small, Scale);

		// One pip a gun: filled if the carriage is still there, hollow if it
		// has been dismounted. Four pips that do not all light is the clearest
		// possible statement that the battery is hurt.
		const float PipX = X + Width * 0.42f;
		const float Pip = Line * 0.34f;
		for (int32 g = 0; g < 4; ++g)
		{
			const float Px = PipX + g * Pip * 1.8f;
			if (g < Mounted)
			{
				DrawRect(Ink, Px, Y + Line * 0.10f, Pip, Pip);
			}
			else
			{
				DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.12f), Px, Y + Line * 0.10f, Pip, Pip);
			}
		}

		// The reload fills up rather than draining away: a full bar means
		// ready, which is the thing you want to read at a glance.
		const float BarX = PipX + 4 * Pip * 1.8f + 10.f * Scale;
		const float BarW = X + Width - BarX;
		if (BarW > 10.f)
		{
			const float F = 1.f - FMath::Clamp(Left / Reload, 0.f, 1.f);
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.10f), BarX, Y + Line * 0.12f, BarW, Pip * 0.8f);
			// A full bar means ready, everywhere on this panel. A battery with
			// no guns left therefore draws NOTHING, not a full bar in red,
			// which anyone scanning bar lengths would have read as loaded.
			if (Mounted > 0)
			{
				DrawRect(F >= 1.f ? Good : Warn, BarX, Y + Line * 0.12f,
					BarW * F, Pip * 0.8f);
			}
		}
		Y += Line * 0.95f;
	}

	// How much of the squadron is still up. Without it a player has no way to
	// tell whether they are winning: hulls out at 300 m all look alike.
	if (const ASeaGameMode* Sea = GetWorld() ? GetWorld()->GetAuthGameMode<ASeaGameMode>() : nullptr)
	{
		const int32 Afloat = Sea->CountEnemiesAfloat();
		const int32 Total = FMath::Max(Afloat, Sea->GetSquadronSize());
		if (Total > 1)
		{
			const FString Text = FString::Printf(TEXT("SQUADRON  %d of %d afloat"), Afloat, Total);
			DrawText(Text, Afloat > 1 ? Warn : Faint, X, Y, Small, Scale);
			Y += Line * 0.95f;
		}
		// The mission, when there is one. A raider who cannot see the tally
		// cannot decide whether the next merchant is worth the beat.
		if (Sea->GetConvoySize() > 0)
		{
			const FString Text = Sea->IsMissionOver()
				? FString::Printf(TEXT("CONVOY %s"), *Sea->GetMissionResult())
				: FString::Printf(TEXT("CONVOY  %d of %d stopped, %d through, need %d"),
					Sea->GetConvoyStopped(), Sea->GetConvoySize(),
					Sea->GetConvoyThrough(), Sea->GetConvoyNeed());
			DrawText(Text, Sea->IsMissionOver() ? Good : Warn, X, Y, Small, Scale);
			Y += Line * 0.95f;
			// What the prizes are worth. It buys nothing yet and the README
			// says so; a number on the panel that implied a shop would be
			// the panel telling a lie the game cannot keep.
			// Men actually away NOW: sent, less those who came home with a
			// prize. The panel must not tell a captain he is short of twelve
			// men who are standing on his own deck again.
			const int32 Away = Sea->GetHandsAwayNow();
			if (Sea->HasPort())
			{
				DrawText(FString::Printf(TEXT("LANDED %d  of %d claimed, %d prize%s home"),
					Sea->GetLanded(), Sea->GetPurse(), Sea->GetPrizesLanded(),
					Sea->GetPrizesLanded() == 1 ? TEXT("") : TEXT("s")),
					Sea->GetLanded() > 0 ? Good : Faint, X, Y, Small, Scale);
				Y += Line * 0.95f;
				// What is left to spend, and what it has bought. Coffers is the
				// number a captain steers by.
				DrawText(Sea->GetSpent() > 0
					? FString::Printf(TEXT("COFFERS %d  spent %d: %d hands, %d hull"),
						Sea->GetCoffers(), Sea->GetSpent(), Sea->GetHandsBought(),
						Sea->GetHullBought())
					: FString::Printf(TEXT("COFFERS %d"), Sea->GetCoffers()),
					Sea->GetCoffers() > 0 ? Good : Faint, X, Y, Small, Scale);
				Y += Line * 0.95f;
			}
			DrawText(Away > 0
				? FString::Printf(TEXT("PURSE  %d  from %d prize%s, %d manned, %d hands away"),
					Sea->GetPurse(), Sea->GetPrizesTaken(),
					Sea->GetPrizesTaken() == 1 ? TEXT("") : TEXT("s"),
					Sea->GetPrizesManned(), Away)
				: FString::Printf(TEXT("PURSE  %d  from %d prize%s"),
					Sea->GetPurse(), Sea->GetPrizesTaken(),
					Sea->GetPrizesTaken() == 1 ? TEXT("") : TEXT("s")),
				Sea->GetPurse() > 0 ? Good : Faint, X, Y, Small, Scale);
		}
	}
}

void AShipHUD::DrawWay(AShipPawn* Ship)
{
	const float X = Canvas->SizeX * 0.030f;
	float Y = Canvas->SizeY * 0.600f;

	DrawRect(Panel, X - 12.f * Scale, Y - Line * 0.9f,
		Canvas->SizeX * 0.235f, Line * 3.5f);
	DrawText(TEXT("WAY"), Faint, X, Y - Line * 0.75f, Small, Scale);

	DrawText(FString::Printf(TEXT("%.1f m/s"), Ship->GetForwardSpeedMS()),
		Ink, X, Y, Big, Scale);
	Y += Line;

	// Ground made good to windward. The number that says whether beating is
	// working: she can show six knots and still be losing ground.
	const float Vmg = Ship->GetWindwardVMG();
	const FLinearColor VmgColour = (Vmg > 0.05f) ? Good : (Vmg < -0.05f ? Bad : Faint);
	DrawText(FString::Printf(TEXT("%+.2f to windward"), Vmg), VmgColour, X, Y, Small, Scale);
	Y += Line * 0.8f;

	DrawText(FString::Printf(TEXT("%.0f deg leeway"), FMath::Abs(Ship->GetLeewayDeg())),
		Faint, X, Y, Small, Scale);
}

void AShipHUD::DrawWarnings(AShipPawn* Ship, UWindSubsystem* Wind)
{
	TArray<TPair<FString, FLinearColor>> Lines;

	if (Ship->IsSinking())
	{
		Lines.Add(TPair<FString, FLinearColor>(TEXT("SHE IS GOING DOWN"), Bad));
	}
	else
	{
		if (Wind && Ship->GetWindAngleDeg() < Ship->GetNoGoAngleDeg())
		{
			Lines.Add(TPair<FString, FLinearColor>(TEXT("IN IRONS - BEAR AWAY"), Warn));
		}
		if (Ship->GetRigEfficiency() <= 0.02f)
		{
			Lines.Add(TPair<FString, FLinearColor>(TEXT("DISMASTED"), Bad));
		}
		if (Ship->GetRudderIntegrity() <= 0.02f)
		{
			Lines.Add(TPair<FString, FLinearColor>(TEXT("RUDDER GONE"), Bad));
		}
		// A rudder needs water flowing past it, and a ship lying still with her
		// canvas furled has neither way on nor backed yards: she cannot turn at
		// all, and nothing else on screen would ever say why.
		if (FMath::Abs(Ship->GetForwardSpeedMS()) < 0.3f && Ship->GetSailTrim() <= 0.1f)
		{
			Lines.Add(TPair<FString, FLinearColor>(TEXT("NO WAY ON - SET SAIL TO STEER"), Warn));
		}
	}

	UFont* Shout = GEngine->GetLargeFont();
	float Y = Canvas->SizeY * 0.115f;
	for (const TPair<FString, FLinearColor>& Item : Lines)
	{
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Shout, Item.Key, W, H, Scale, Scale);
		// A bar behind it: red text on a white sail is not a warning, it is
		// a rumour.
		DrawRect(FLinearColor(0.02f, 0.02f, 0.03f, 0.62f),
			(Canvas->SizeX - W) * 0.5f - 16.f * Scale, Y - 4.f * Scale,
			W + 32.f * Scale, H + 8.f * Scale);
		DrawText(Item.Key, Item.Value, (Canvas->SizeX - W) * 0.5f, Y, Shout, Scale);
		Y += H + 12.f * Scale;
	}
}
