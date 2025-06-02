#include "MainDungeonGenerator.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/EngineTypes.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"

AMainDungeonGenerator::AMainDungeonGenerator()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AMainDungeonGenerator::BeginPlay()
{
    Super::BeginPlay();
    GenerateDungeon();
}

void AMainDungeonGenerator::GenerateDungeon()
{
    // Ensure required blueprints are set
    if (!FirstRoomBlueprint || CorridorBlueprints.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No first room or corridor blueprints set."));
        return;
    }

    // Generate the random seed if it is not set (RandomSeed == 0)
    if (RandomSeed == 0)
    {
        RandomSeed = FMath::Rand(); // Generate a random seed
        UE_LOG(LogTemp, Warning, TEXT("RandomSeed not provided; using generated seed: %d"), RandomSeed);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Using provided RandomSeed: %d"), RandomSeed);
    }

    // Initialize the RNG with the seed
    DungeonRNG.Initialize(RandomSeed);

    FVector Location = GetActorLocation();
    FRotator Rotation = FRotator::ZeroRotator;

    // Spawn the first room
    AActor* FirstRoom = GetWorld()->SpawnActor<AActor>(FirstRoomBlueprint, Location, Rotation);
    if (!FirstRoom) return;

    SpawnedRooms.Add(FirstRoom);
    RoomsSpawned = 1;

    // Start generating the dungeon
    while (RoomsSpawned < NumRoomsToGenerate)
    {
        GenerateNextRoom();
    }

    UE_LOG(LogTemp, Warning, TEXT("Dungeon generation complete. Total rooms: %d"), RoomsSpawned);
}

void AMainDungeonGenerator::GenerateNextRoom()
{
    if (RoomsSpawned >= NumRoomsToGenerate)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dungeon generation complete."));
        return;
    }

    // Collect all available exits
    TArray<UArrowComponent*> AllAvailableExits;
    for (AActor* Room : SpawnedRooms)
    {
        TArray<UArrowComponent*> RoomExits = GetExitArrows(Room);
        for (UArrowComponent* Exit : RoomExits)
        {
            if (!UsedArrows.Contains(Exit))
            {
                AllAvailableExits.Add(Exit);
            }
        }
    }

    if (AllAvailableExits.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No more available exits."));
        return;
    }

    while (AllAvailableExits.Num() > 0)
    {
        // Randomly select an exit
        int32 Index = DungeonRNG.RandRange(0, AllAvailableExits.Num() - 1);
        UArrowComponent* ExitArrow = AllAvailableExits[Index];
        AllAvailableExits.RemoveAt(Index);

        // Randomly select a corridor blueprint
        int32 CorridorIndex = DungeonRNG.RandRange(0, CorridorBlueprints.Num() - 1);
        TSubclassOf<AActor> CorridorClass = CorridorBlueprints[CorridorIndex];

        UE_LOG(LogTemp, Warning, TEXT("Attempting to spawn corridor: %s"), *GetNameSafe(CorridorClass.Get()));

        AActor* Corridor = GetWorld()->SpawnActor<AActor>(CorridorClass);
        if (!Corridor) continue;

        TArray<UArrowComponent*> CorridorArrows = GetExitArrows(Corridor);
        if (CorridorArrows.Num() < 2)
        {
            UE_LOG(LogTemp, Error, TEXT("Corridor missing arrows: %s"), *Corridor->GetName());
            Corridor->Destroy();
            continue;
        }

        UArrowComponent* CorridorStart = CorridorArrows[0];
        UArrowComponent* CorridorEnd = CorridorArrows[1];

        AlignActorToArrow(ExitArrow, CorridorStart, Corridor);

        // Determine whether to spawn a shop on the rooms' last iteration
        bool bPlaceShop = (RoomsSpawned == NumRoomsToGenerate - 1 && ShopBlueprint); 
        TSubclassOf<AActor> RoomClass = bPlaceShop ? ShopBlueprint : RoomBlueprints[DungeonRNG.RandRange(0, RoomBlueprints.Num() - 1)];

        UE_LOG(LogTemp, Warning, TEXT("Attempting to spawn %s at available exit."),
            *GetNameSafe(RoomClass.Get()));

        AActor* NextRoom = GetWorld()->SpawnActor<AActor>(RoomClass);
        if (!NextRoom)
        {
            Corridor->Destroy();
            continue;
        }

        TArray<UArrowComponent*> Entrances = GetExitArrows(NextRoom);
        if (Entrances.Num() == 0)
        {
            Corridor->Destroy();
            NextRoom->Destroy();
            continue;
        }

        UArrowComponent* EntranceArrow = Entrances[0];
        AlignActorToArrow(CorridorEnd, EntranceArrow, NextRoom);

        if (CheckOverlap(Corridor) || CheckOverlap(NextRoom))
        {
            Corridor->Destroy();
            NextRoom->Destroy();
            UsedArrows.Add(ExitArrow);
            continue;
        }

        UsedArrows.Add(ExitArrow);
        SpawnedRooms.Add(NextRoom);
        RoomsSpawned++;

        if (bPlaceShop)
        {
            UE_LOG(LogTemp, Warning, TEXT("Shop room successfully placed on exit: %s"), *ExitArrow->GetName());
        }

        if (ExitArrow && ExitArrow->IsValidLowLevel())
        {
            UE_LOG(LogTemp, Log, TEXT("Destroying used arrow: %s"), *ExitArrow->GetName());
            ExitArrow->DestroyComponent();
        }

        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("Room %d failed to place."), RoomsSpawned);
}

void AMainDungeonGenerator::AlignActorToArrow(UArrowComponent* TargetArrow, UArrowComponent* SourceArrow, AActor* ActorToMove)
{
    if (!TargetArrow || !SourceArrow || !ActorToMove) return;

    FVector TargetLocation = TargetArrow->GetComponentLocation();
    FVector TargetForward = TargetArrow->GetForwardVector();
    FVector SourceLocation = SourceArrow->GetComponentLocation();

    FRotator AlignRotation = UKismetMathLibrary::MakeRotFromXZ(-TargetForward, FVector::UpVector);
    FQuat SourceRelativeRot = SourceArrow->GetComponentQuat().Inverse() * ActorToMove->GetActorQuat();
    FQuat FinalRot = AlignRotation.Quaternion() * SourceRelativeRot;

    FVector Offset = SourceLocation - ActorToMove->GetActorLocation();
    FVector AlignedLocation = TargetLocation - FinalRot.RotateVector(Offset);

    ActorToMove->SetActorLocationAndRotation(AlignedLocation, FinalRot.Rotator());
}

TArray<UArrowComponent*> AMainDungeonGenerator::GetExitArrows(AActor* RoomActor, const FName& ExitListName)
{
    TArray<UArrowComponent*> Arrows;
    if (!RoomActor) return Arrows;

    TArray<USceneComponent*> Components;
    RoomActor->GetComponents<USceneComponent>(Components);

    for (USceneComponent* Comp : Components)
    {
        if (Comp->GetName() == ExitListName.ToString())
        {
            TArray<USceneComponent*> LocalChildren;
            Comp->GetChildrenComponents(true, LocalChildren);

            for (USceneComponent* Child : LocalChildren)
            {
                if (UArrowComponent* Arrow = Cast<UArrowComponent>(Child))
                {
                    Arrows.Add(Arrow);
                }
            }
            break;
        }
    }

    return Arrows;
}

bool AMainDungeonGenerator::CheckOverlap(AActor* Actor)
{
    if (!Actor) return true;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(Actor);

    UBoxComponent* Collider = Actor->FindComponentByClass<UBoxComponent>();
    if (!Collider)
    {
        UE_LOG(LogTemp, Warning, TEXT("No Collider found on: %s"), *Actor->GetName());
        return false;
    }

    FVector Location = Collider->GetComponentLocation();
    FVector Extent = Collider->GetScaledBoxExtent();
    FQuat Rotation = Collider->GetComponentQuat();

    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByChannel(
        Overlaps,
        Location,
        Rotation,
        ECC_WorldDynamic,
        FCollisionShape::MakeBox(Extent),
        QueryParams
    );

    for (const FOverlapResult& Result : Overlaps)
    {
        AActor* OverlapActor = Result.GetActor();
        if (OverlapActor && OverlapActor != Actor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Overlap with: %s"), *OverlapActor->GetName());
            return true;
        }
    }

    return false;
}