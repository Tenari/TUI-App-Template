#include "base/all.h"

#ifndef GAMESHARED_H
#define GAMESHARED_H

///// #define some game-tunable constants
#define GAME_CONSTANT_ONE (1)
#define GAME_CONSTANT_TWO (2)
#define THING_HEADER_MESSAGE_SIZE (8+8+1+1+1)
#define THING_MESSAGE_SIZE (THING_HEADER_MESSAGE_SIZE+2+2+2+2+8+1+1)

typedef enum ThingFeature {
  FeatureWalksAround,
  FeatureCanFight,
  ThingFeature_Count
} ThingFeature;

typedef enum ThingType {
  ThingNull,
  ThingWall,
  ThingDoor,
  ThingCharacter,
  ThingType_Count,
} ThingType;

static const char* THING_TYPE_STRINGS[] = {
  "NULL",
  "Wall",
  "Door",
  "Character",
};

typedef struct {
  i32 x;
  i32 y;
  i32 z;
} XYZ;

typedef enum Direction {
  DirectionInvalid,
  North,
  South,
  East,
  West,
  Up,
  Down,
  Direction_Count
} Direction;

typedef enum CommandType {
  CommandInvalid,
  CommandKeepAlive,
  CommandLogin,
  CommandCreateCharacter,
  CommandType_Count,
} CommandType;
static const char* command_type_strings[] = {
  "Invalid",
  "KeepAlive",
  "Login",
  "CreateCharacter",
};

typedef enum Message {
  MessageInvalid,
  MessageCharacterId,
  MessageBadPw,
  MessageNewAccountCreated,
  Message_Count,
} Message;
static const char* MESSAGE_STRINGS[] = {
  "Invalid",
  "CharacterId",
  "BadPw",
  "NewAccountCreated",
};

#endif //GAMESHARED_H
