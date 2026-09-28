#include "core/TextProcessor.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>

namespace {

// Common emoji and how they are read aloud. Code points rather than literals so
// the source stays plain ASCII.
struct EmojiName
{
    char32_t codePoint;
    const char *name;
};

const EmojiName kEmojiNames[] = {
    // Faces
    {0x1F600, QT_TRANSLATE_NOOP("Emoji", "grinning")},
    {0x1F603, QT_TRANSLATE_NOOP("Emoji", "smiling")},
    {0x1F604, QT_TRANSLATE_NOOP("Emoji", "smiling")},
    {0x1F601, QT_TRANSLATE_NOOP("Emoji", "grinning")},
    {0x1F606, QT_TRANSLATE_NOOP("Emoji", "laughing")},
    {0x1F605, QT_TRANSLATE_NOOP("Emoji", "nervous laugh")},
    {0x1F923, QT_TRANSLATE_NOOP("Emoji", "rolling on the floor laughing")},
    {0x1F602, QT_TRANSLATE_NOOP("Emoji", "laughing")},
    {0x1F642, QT_TRANSLATE_NOOP("Emoji", "smile")},
    {0x1F643, QT_TRANSLATE_NOOP("Emoji", "upside down face")},
    {0x1F609, QT_TRANSLATE_NOOP("Emoji", "wink")},
    {0x1F60A, QT_TRANSLATE_NOOP("Emoji", "smiling")},
    {0x1F607, QT_TRANSLATE_NOOP("Emoji", "angel")},
    {0x1F970, QT_TRANSLATE_NOOP("Emoji", "in love")},
    {0x1F60D, QT_TRANSLATE_NOOP("Emoji", "heart eyes")},
    {0x1F929, QT_TRANSLATE_NOOP("Emoji", "star struck")},
    {0x1F618, QT_TRANSLATE_NOOP("Emoji", "blowing a kiss")},
    {0x1F617, QT_TRANSLATE_NOOP("Emoji", "kiss")},
    {0x1F61A, QT_TRANSLATE_NOOP("Emoji", "kiss")},
    {0x1F619, QT_TRANSLATE_NOOP("Emoji", "kiss")},
    {0x1F972, QT_TRANSLATE_NOOP("Emoji", "happy tears")},
    {0x1F979, QT_TRANSLATE_NOOP("Emoji", "holding back tears")},
    {0x1F60B, QT_TRANSLATE_NOOP("Emoji", "yum")},
    {0x1F61B, QT_TRANSLATE_NOOP("Emoji", "tongue out")},
    {0x1F61C, QT_TRANSLATE_NOOP("Emoji", "silly")},
    {0x1F92A, QT_TRANSLATE_NOOP("Emoji", "crazy")},
    {0x1F61D, QT_TRANSLATE_NOOP("Emoji", "tongue out")},
    {0x1F911, QT_TRANSLATE_NOOP("Emoji", "money face")},
    {0x1F917, QT_TRANSLATE_NOOP("Emoji", "hug")},
    {0x1F92D, QT_TRANSLATE_NOOP("Emoji", "oops")},
    {0x1FAE2, QT_TRANSLATE_NOOP("Emoji", "gasp")},
    {0x1FAE3, QT_TRANSLATE_NOOP("Emoji", "peeking")},
    {0x1F92B, QT_TRANSLATE_NOOP("Emoji", "shh")},
    {0x1F914, QT_TRANSLATE_NOOP("Emoji", "thinking")},
    {0x1FAE1, QT_TRANSLATE_NOOP("Emoji", "salute")},
    {0x1F910, QT_TRANSLATE_NOOP("Emoji", "zipped lips")},
    {0x1F928, QT_TRANSLATE_NOOP("Emoji", "raised eyebrow")},
    {0x1F610, QT_TRANSLATE_NOOP("Emoji", "meh")},
    {0x1F611, QT_TRANSLATE_NOOP("Emoji", "blank stare")},
    {0x1F636, QT_TRANSLATE_NOOP("Emoji", "speechless")},
    {0x1FAE5, QT_TRANSLATE_NOOP("Emoji", "speechless")},
    {0x1F60F, QT_TRANSLATE_NOOP("Emoji", "smirk")},
    {0x1F612, QT_TRANSLATE_NOOP("Emoji", "unamused")},
    {0x1F644, QT_TRANSLATE_NOOP("Emoji", "eye roll")},
    {0x1F62C, QT_TRANSLATE_NOOP("Emoji", "yikes")},
    {0x1F925, QT_TRANSLATE_NOOP("Emoji", "lying")},
    {0x1FAE0, QT_TRANSLATE_NOOP("Emoji", "melting")},
    {0x1F60C, QT_TRANSLATE_NOOP("Emoji", "relieved")},
    {0x1F614, QT_TRANSLATE_NOOP("Emoji", "sad")},
    {0x1F62A, QT_TRANSLATE_NOOP("Emoji", "sleepy")},
    {0x1F924, QT_TRANSLATE_NOOP("Emoji", "drooling")},
    {0x1F634, QT_TRANSLATE_NOOP("Emoji", "sleeping")},
    {0x1F637, QT_TRANSLATE_NOOP("Emoji", "sick")},
    {0x1F912, QT_TRANSLATE_NOOP("Emoji", "sick")},
    {0x1F915, QT_TRANSLATE_NOOP("Emoji", "hurt")},
    {0x1F922, QT_TRANSLATE_NOOP("Emoji", "nauseous")},
    {0x1F92E, QT_TRANSLATE_NOOP("Emoji", "throwing up")},
    {0x1F927, QT_TRANSLATE_NOOP("Emoji", "sneezing")},
    {0x1F975, QT_TRANSLATE_NOOP("Emoji", "hot")},
    {0x1F976, QT_TRANSLATE_NOOP("Emoji", "freezing")},
    {0x1F974, QT_TRANSLATE_NOOP("Emoji", "dizzy")},
    {0x1F635, QT_TRANSLATE_NOOP("Emoji", "dizzy")},
    {0x1F92F, QT_TRANSLATE_NOOP("Emoji", "mind blown")},
    {0x1F920, QT_TRANSLATE_NOOP("Emoji", "cowboy")},
    {0x1F973, QT_TRANSLATE_NOOP("Emoji", "party")},
    {0x1F60E, QT_TRANSLATE_NOOP("Emoji", "cool")},
    {0x1F913, QT_TRANSLATE_NOOP("Emoji", "nerd")},
    {0x1F9D0, QT_TRANSLATE_NOOP("Emoji", "hmm")},
    {0x1F615, QT_TRANSLATE_NOOP("Emoji", "confused")},
    {0x1FAE4, QT_TRANSLATE_NOOP("Emoji", "meh")},
    {0x1F61F, QT_TRANSLATE_NOOP("Emoji", "worried")},
    {0x1F641, QT_TRANSLATE_NOOP("Emoji", "frown")},
    {0x2639, QT_TRANSLATE_NOOP("Emoji", "frown")},
    {0x1F62E, QT_TRANSLATE_NOOP("Emoji", "surprised")},
    {0x1F62F, QT_TRANSLATE_NOOP("Emoji", "surprised")},
    {0x1F632, QT_TRANSLATE_NOOP("Emoji", "shocked")},
    {0x1F633, QT_TRANSLATE_NOOP("Emoji", "embarrassed")},
    {0x1F97A, QT_TRANSLATE_NOOP("Emoji", "puppy eyes")},
    {0x1F626, QT_TRANSLATE_NOOP("Emoji", "oh no")},
    {0x1F627, QT_TRANSLATE_NOOP("Emoji", "anguished")},
    {0x1F628, QT_TRANSLATE_NOOP("Emoji", "scared")},
    {0x1F630, QT_TRANSLATE_NOOP("Emoji", "anxious")},
    {0x1F625, QT_TRANSLATE_NOOP("Emoji", "disappointed")},
    {0x1F622, QT_TRANSLATE_NOOP("Emoji", "sad")},
    {0x1F62D, QT_TRANSLATE_NOOP("Emoji", "crying")},
    {0x1F631, QT_TRANSLATE_NOOP("Emoji", "screaming")},
    {0x1F616, QT_TRANSLATE_NOOP("Emoji", "frustrated")},
    {0x1F623, QT_TRANSLATE_NOOP("Emoji", "struggling")},
    {0x1F61E, QT_TRANSLATE_NOOP("Emoji", "disappointed")},
    {0x1F613, QT_TRANSLATE_NOOP("Emoji", "phew")},
    {0x1F629, QT_TRANSLATE_NOOP("Emoji", "so tired")},
    {0x1F62B, QT_TRANSLATE_NOOP("Emoji", "tired")},
    {0x1F971, QT_TRANSLATE_NOOP("Emoji", "yawn")},
    {0x1F624, QT_TRANSLATE_NOOP("Emoji", "frustrated")},
    {0x1F621, QT_TRANSLATE_NOOP("Emoji", "angry")},
    {0x1F620, QT_TRANSLATE_NOOP("Emoji", "angry")},
    {0x1F92C, QT_TRANSLATE_NOOP("Emoji", "cursing")},
    {0x1F608, QT_TRANSLATE_NOOP("Emoji", "mischievous")},
    {0x1F47F, QT_TRANSLATE_NOOP("Emoji", "devil")},
    {0x1F480, QT_TRANSLATE_NOOP("Emoji", "skull")},
    {0x2620, QT_TRANSLATE_NOOP("Emoji", "skull")},
    {0x1F4A9, QT_TRANSLATE_NOOP("Emoji", "poop")},
    {0x1F921, QT_TRANSLATE_NOOP("Emoji", "clown")},
    {0x1F47B, QT_TRANSLATE_NOOP("Emoji", "ghost")},
    {0x1F47D, QT_TRANSLATE_NOOP("Emoji", "alien")},
    {0x1F916, QT_TRANSLATE_NOOP("Emoji", "robot")},
    {0x1F63A, QT_TRANSLATE_NOOP("Emoji", "happy cat")},
    {0x1F639, QT_TRANSLATE_NOOP("Emoji", "laughing cat")},
    {0x1F63B, QT_TRANSLATE_NOOP("Emoji", "heart eyes cat")},
    {0x1F648, QT_TRANSLATE_NOOP("Emoji", "see no evil")},
    {0x1F649, QT_TRANSLATE_NOOP("Emoji", "hear no evil")},
    {0x1F64A, QT_TRANSLATE_NOOP("Emoji", "speak no evil")},
    // Hearts and symbols
    {0x2764, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x2665, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x1F9E1, QT_TRANSLATE_NOOP("Emoji", "orange heart")},
    {0x1F49B, QT_TRANSLATE_NOOP("Emoji", "yellow heart")},
    {0x1F49A, QT_TRANSLATE_NOOP("Emoji", "green heart")},
    {0x1F499, QT_TRANSLATE_NOOP("Emoji", "blue heart")},
    {0x1F49C, QT_TRANSLATE_NOOP("Emoji", "purple heart")},
    {0x1F5A4, QT_TRANSLATE_NOOP("Emoji", "black heart")},
    {0x1F90D, QT_TRANSLATE_NOOP("Emoji", "white heart")},
    {0x1F90E, QT_TRANSLATE_NOOP("Emoji", "brown heart")},
    {0x1FA77, QT_TRANSLATE_NOOP("Emoji", "pink heart")},
    {0x1F494, QT_TRANSLATE_NOOP("Emoji", "broken heart")},
    {0x1F495, QT_TRANSLATE_NOOP("Emoji", "hearts")},
    {0x1F49E, QT_TRANSLATE_NOOP("Emoji", "hearts")},
    {0x1F493, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x1F497, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x1F496, QT_TRANSLATE_NOOP("Emoji", "sparkling heart")},
    {0x1F498, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x1F49D, QT_TRANSLATE_NOOP("Emoji", "heart")},
    {0x1F48B, QT_TRANSLATE_NOOP("Emoji", "kiss")},
    {0x1F4AF, QT_TRANSLATE_NOOP("Emoji", "one hundred percent")},
    {0x1F4A2, QT_TRANSLATE_NOOP("Emoji", "anger")},
    {0x1F4A5, QT_TRANSLATE_NOOP("Emoji", "boom")},
    {0x1F4AB, QT_TRANSLATE_NOOP("Emoji", "dizzy")},
    {0x1F4A6, QT_TRANSLATE_NOOP("Emoji", "splash")},
    {0x1F4A8, QT_TRANSLATE_NOOP("Emoji", "zoom")},
    {0x1F4AC, QT_TRANSLATE_NOOP("Emoji", "speech bubble")},
    {0x1F4AD, QT_TRANSLATE_NOOP("Emoji", "thinking")},
    {0x1F4A4, QT_TRANSLATE_NOOP("Emoji", "zzz")},
    // Hands and people
    {0x1F44B, QT_TRANSLATE_NOOP("Emoji", "wave")},
    {0x1F91A, QT_TRANSLATE_NOOP("Emoji", "raised hand")},
    {0x270B, QT_TRANSLATE_NOOP("Emoji", "raised hand")},
    {0x1F590, QT_TRANSLATE_NOOP("Emoji", "hand")},
    {0x1F596, QT_TRANSLATE_NOOP("Emoji", "live long and prosper")},
    {0x1F44C, QT_TRANSLATE_NOOP("Emoji", "OK")},
    {0x1F90C, QT_TRANSLATE_NOOP("Emoji", "pinched fingers")},
    {0x1F90F, QT_TRANSLATE_NOOP("Emoji", "a little bit")},
    {0x270C, QT_TRANSLATE_NOOP("Emoji", "peace")},
    {0x1F91E, QT_TRANSLATE_NOOP("Emoji", "fingers crossed")},
    {0x1F91F, QT_TRANSLATE_NOOP("Emoji", "love you")},
    {0x1F918, QT_TRANSLATE_NOOP("Emoji", "rock on")},
    {0x1F919, QT_TRANSLATE_NOOP("Emoji", "call me")},
    {0x1F448, QT_TRANSLATE_NOOP("Emoji", "left")},
    {0x1F449, QT_TRANSLATE_NOOP("Emoji", "right")},
    {0x1F446, QT_TRANSLATE_NOOP("Emoji", "up")},
    {0x1F447, QT_TRANSLATE_NOOP("Emoji", "down")},
    {0x261D, QT_TRANSLATE_NOOP("Emoji", "this")},
    {0x1F44D, QT_TRANSLATE_NOOP("Emoji", "thumbs up")},
    {0x1F44E, QT_TRANSLATE_NOOP("Emoji", "thumbs down")},
    {0x270A, QT_TRANSLATE_NOOP("Emoji", "fist")},
    {0x1F44A, QT_TRANSLATE_NOOP("Emoji", "fist bump")},
    {0x1F91B, QT_TRANSLATE_NOOP("Emoji", "fist bump")},
    {0x1F91C, QT_TRANSLATE_NOOP("Emoji", "fist bump")},
    {0x1F44F, QT_TRANSLATE_NOOP("Emoji", "clapping")},
    {0x1F64C, QT_TRANSLATE_NOOP("Emoji", "hooray")},
    {0x1FAF6, QT_TRANSLATE_NOOP("Emoji", "heart hands")},
    {0x1F450, QT_TRANSLATE_NOOP("Emoji", "open hands")},
    {0x1F932, QT_TRANSLATE_NOOP("Emoji", "open hands")},
    {0x1F91D, QT_TRANSLATE_NOOP("Emoji", "handshake")},
    {0x1F64F, QT_TRANSLATE_NOOP("Emoji", "please")},
    {0x270D, QT_TRANSLATE_NOOP("Emoji", "writing")},
    {0x1F4AA, QT_TRANSLATE_NOOP("Emoji", "strong")},
    {0x1F440, QT_TRANSLATE_NOOP("Emoji", "eyes")},
    {0x1F441, QT_TRANSLATE_NOOP("Emoji", "eye")},
    {0x1F9E0, QT_TRANSLATE_NOOP("Emoji", "brain")},
    {0x1F476, QT_TRANSLATE_NOOP("Emoji", "baby")},
    {0x1F466, QT_TRANSLATE_NOOP("Emoji", "boy")},
    {0x1F467, QT_TRANSLATE_NOOP("Emoji", "girl")},
    {0x1F468, QT_TRANSLATE_NOOP("Emoji", "man")},
    {0x1F469, QT_TRANSLATE_NOOP("Emoji", "woman")},
    {0x1F9D1, QT_TRANSLATE_NOOP("Emoji", "person")},
    {0x1F46A, QT_TRANSLATE_NOOP("Emoji", "family")},
    {0x1F937, QT_TRANSLATE_NOOP("Emoji", "shrug")},
    {0x1F926, QT_TRANSLATE_NOOP("Emoji", "facepalm")},
    {0x1F64B, QT_TRANSLATE_NOOP("Emoji", "raising hand")},
    {0x1F646, QT_TRANSLATE_NOOP("Emoji", "OK")},
    {0x1F645, QT_TRANSLATE_NOOP("Emoji", "no")},
    {0x1F647, QT_TRANSLATE_NOOP("Emoji", "bowing")},
    {0x1F3C3, QT_TRANSLATE_NOOP("Emoji", "running")},
    {0x1F483, QT_TRANSLATE_NOOP("Emoji", "dancing")},
    {0x1F57A, QT_TRANSLATE_NOOP("Emoji", "dancing")},
    {0x1F451, QT_TRANSLATE_NOOP("Emoji", "crown")},
    {0x1F48E, QT_TRANSLATE_NOOP("Emoji", "diamond")},
    // Things, nature, food
    {0x1F525, QT_TRANSLATE_NOOP("Emoji", "fire")},
    {0x2728, QT_TRANSLATE_NOOP("Emoji", "sparkles")},
    {0x2B50, QT_TRANSLATE_NOOP("Emoji", "star")},
    {0x1F31F, QT_TRANSLATE_NOOP("Emoji", "star")},
    {0x26A1, QT_TRANSLATE_NOOP("Emoji", "lightning")},
    {0x2600, QT_TRANSLATE_NOOP("Emoji", "sun")},
    {0x1F308, QT_TRANSLATE_NOOP("Emoji", "rainbow")},
    {0x2614, QT_TRANSLATE_NOOP("Emoji", "rain")},
    {0x2744, QT_TRANSLATE_NOOP("Emoji", "snowflake")},
    {0x1F319, QT_TRANSLATE_NOOP("Emoji", "moon")},
    {0x1F30D, QT_TRANSLATE_NOOP("Emoji", "world")},
    {0x1F30E, QT_TRANSLATE_NOOP("Emoji", "world")},
    {0x1F30F, QT_TRANSLATE_NOOP("Emoji", "world")},
    {0x1F339, QT_TRANSLATE_NOOP("Emoji", "rose")},
    {0x1F338, QT_TRANSLATE_NOOP("Emoji", "flower")},
    {0x1F33B, QT_TRANSLATE_NOOP("Emoji", "sunflower")},
    {0x1F340, QT_TRANSLATE_NOOP("Emoji", "good luck")},
    {0x1F389, QT_TRANSLATE_NOOP("Emoji", "celebration")},
    {0x1F38A, QT_TRANSLATE_NOOP("Emoji", "confetti")},
    {0x1F388, QT_TRANSLATE_NOOP("Emoji", "balloon")},
    {0x1F382, QT_TRANSLATE_NOOP("Emoji", "birthday cake")},
    {0x1F381, QT_TRANSLATE_NOOP("Emoji", "gift")},
    {0x1F3C6, QT_TRANSLATE_NOOP("Emoji", "trophy")},
    {0x1F947, QT_TRANSLATE_NOOP("Emoji", "gold medal")},
    {0x1F3AE, QT_TRANSLATE_NOOP("Emoji", "video game")},
    {0x1F3AF, QT_TRANSLATE_NOOP("Emoji", "bullseye")},
    {0x1F3B5, QT_TRANSLATE_NOOP("Emoji", "music")},
    {0x1F3B6, QT_TRANSLATE_NOOP("Emoji", "music")},
    {0x1F3A4, QT_TRANSLATE_NOOP("Emoji", "microphone")},
    {0x1F3A7, QT_TRANSLATE_NOOP("Emoji", "headphones")},
    {0x1F4F7, QT_TRANSLATE_NOOP("Emoji", "camera")},
    {0x1F4F8, QT_TRANSLATE_NOOP("Emoji", "camera")},
    {0x1F4F1, QT_TRANSLATE_NOOP("Emoji", "phone")},
    {0x1F4BB, QT_TRANSLATE_NOOP("Emoji", "laptop")},
    {0x2615, QT_TRANSLATE_NOOP("Emoji", "coffee")},
    {0x1F355, QT_TRANSLATE_NOOP("Emoji", "pizza")},
    {0x1F354, QT_TRANSLATE_NOOP("Emoji", "burger")},
    {0x1F35F, QT_TRANSLATE_NOOP("Emoji", "fries")},
    {0x1F37F, QT_TRANSLATE_NOOP("Emoji", "popcorn")},
    {0x1F370, QT_TRANSLATE_NOOP("Emoji", "cake")},
    {0x1F37A, QT_TRANSLATE_NOOP("Emoji", "beer")},
    {0x1F37B, QT_TRANSLATE_NOOP("Emoji", "cheers")},
    {0x1F942, QT_TRANSLATE_NOOP("Emoji", "cheers")},
    {0x1F377, QT_TRANSLATE_NOOP("Emoji", "wine")},
    {0x1F4B0, QT_TRANSLATE_NOOP("Emoji", "money")},
    {0x1F4B8, QT_TRANSLATE_NOOP("Emoji", "money")},
    {0x1F680, QT_TRANSLATE_NOOP("Emoji", "rocket")},
    {0x1F697, QT_TRANSLATE_NOOP("Emoji", "car")},
    {0x2708, QT_TRANSLATE_NOOP("Emoji", "plane")},
    {0x1F3E0, QT_TRANSLATE_NOOP("Emoji", "home")},
    {0x23F0, QT_TRANSLATE_NOOP("Emoji", "alarm clock")},
    {0x231B, QT_TRANSLATE_NOOP("Emoji", "waiting")},
    {0x23F3, QT_TRANSLATE_NOOP("Emoji", "waiting")},
    {0x2705, QT_TRANSLATE_NOOP("Emoji", "check")},
    {0x2714, QT_TRANSLATE_NOOP("Emoji", "check")},
    {0x274C, QT_TRANSLATE_NOOP("Emoji", "cross")},
    {0x2757, QT_TRANSLATE_NOOP("Emoji", "exclamation mark")},
    {0x2753, QT_TRANSLATE_NOOP("Emoji", "question mark")},
    {0x26A0, QT_TRANSLATE_NOOP("Emoji", "warning")},
    {0x1F6AB, QT_TRANSLATE_NOOP("Emoji", "not allowed")},
    {0x1F6A8, QT_TRANSLATE_NOOP("Emoji", "alert")},
    {0x1F4A1, QT_TRANSLATE_NOOP("Emoji", "idea")},
    {0x1F4CC, QT_TRANSLATE_NOOP("Emoji", "pin")},
    {0x1F514, QT_TRANSLATE_NOOP("Emoji", "bell")},
    {0x1F5FF, QT_TRANSLATE_NOOP("Emoji", "stone face")},
    {0x1F9E2, QT_TRANSLATE_NOOP("Emoji", "cap")},
    {0x1F197, QT_TRANSLATE_NOOP("Emoji", "OK")},
    {0x1F192, QT_TRANSLATE_NOOP("Emoji", "cool")},
    {0x1F195, QT_TRANSLATE_NOOP("Emoji", "new")},
    {0x1F198, QT_TRANSLATE_NOOP("Emoji", "SOS")},
    {0x1F3F3, QT_TRANSLATE_NOOP("Emoji", "white flag")},
    {0x1F3F4, QT_TRANSLATE_NOOP("Emoji", "flag")},
    {0x1F6A9, QT_TRANSLATE_NOOP("Emoji", "red flag")},
    // Animals
    {0x1F436, QT_TRANSLATE_NOOP("Emoji", "dog")},
    {0x1F415, QT_TRANSLATE_NOOP("Emoji", "dog")},
    {0x1F431, QT_TRANSLATE_NOOP("Emoji", "cat")},
    {0x1F408, QT_TRANSLATE_NOOP("Emoji", "cat")},
    {0x1F438, QT_TRANSLATE_NOOP("Emoji", "frog")},
    {0x1F984, QT_TRANSLATE_NOOP("Emoji", "unicorn")},
    {0x1F40D, QT_TRANSLATE_NOOP("Emoji", "snake")},
    {0x1F410, QT_TRANSLATE_NOOP("Emoji", "goat")},
    {0x1F98B, QT_TRANSLATE_NOOP("Emoji", "butterfly")},
    {0x1F43B, QT_TRANSLATE_NOOP("Emoji", "bear")},
    {0x1F43C, QT_TRANSLATE_NOOP("Emoji", "panda")},
    {0x1F98A, QT_TRANSLATE_NOOP("Emoji", "fox")},
    {0x1F437, QT_TRANSLATE_NOOP("Emoji", "pig")},
    {0x1F435, QT_TRANSLATE_NOOP("Emoji", "monkey")},
    {0x1F412, QT_TRANSLATE_NOOP("Emoji", "monkey")},
    {0x1F422, QT_TRANSLATE_NOOP("Emoji", "turtle")},
    {0x1F41D, QT_TRANSLATE_NOOP("Emoji", "bee")},
    {0x1F988, QT_TRANSLATE_NOOP("Emoji", "shark")},
    {0x1F427, QT_TRANSLATE_NOOP("Emoji", "penguin")},
    {0x1F989, QT_TRANSLATE_NOOP("Emoji", "owl")},
};

// ZWJ sequences with their own meaning (presentation selectors and skin tones removed).
struct EmojiSequence
{
    char32_t codePoints[4];
    const char *name;
};

const EmojiSequence kEmojiSequences[] = {
    {{0x2764, 0x1F525}, QT_TRANSLATE_NOOP("Emoji", "heart on fire")},
    {{0x2764, 0x1FA79}, QT_TRANSLATE_NOOP("Emoji", "mending heart")},
    {{0x1F3F3, 0x1F308}, QT_TRANSLATE_NOOP("Emoji", "rainbow flag")},
    {{0x1F3F3, 0x26A7}, QT_TRANSLATE_NOOP("Emoji", "transgender flag")},
    {{0x1F3F4, 0x2620}, QT_TRANSLATE_NOOP("Emoji", "pirate flag")},
    {{0x1F62E, 0x1F4A8}, QT_TRANSLATE_NOOP("Emoji", "phew")},
    {{0x1F635, 0x1F4AB}, QT_TRANSLATE_NOOP("Emoji", "dizzy")},
    {{0x1F636, 0x1F32B}, QT_TRANSLATE_NOOP("Emoji", "head in the clouds")},
    {{0x1F415, 0x1F9BA}, QT_TRANSLATE_NOOP("Emoji", "service dog")},
    {{0x1F408, 0x2B1B}, QT_TRANSLATE_NOOP("Emoji", "black cat")},
    {{0x1F9D1, 0x1F4BB}, QT_TRANSLATE_NOOP("Emoji", "at the computer")},
    {{0x1F468, 0x1F4BB}, QT_TRANSLATE_NOOP("Emoji", "at the computer")},
    {{0x1F469, 0x1F4BB}, QT_TRANSLATE_NOOP("Emoji", "at the computer")},
};

QString toString(const char32_t *codePoints, qsizetype count)
{
    return QString::fromUcs4(codePoints, count);
}

const QHash<char32_t, const char *> &emojiNames()
{
    static const QHash<char32_t, const char *> names = [] {
        QHash<char32_t, const char *> h;
        for (const EmojiName &e : kEmojiNames)
            h.insert(e.codePoint, e.name);
        return h;
    }();
    return names;
}

const QHash<QString, const char *> &emojiSequences()
{
    static const QHash<QString, const char *> sequences = [] {
        QHash<QString, const char *> h;
        for (const EmojiSequence &e : kEmojiSequences) {
            qsizetype n = 0;
            while (n < 4 && e.codePoints[n])
                ++n;
            h.insert(toString(e.codePoints, n), e.name);
        }
        return h;
    }();
    return sequences;
}

bool isPictographic(char32_t c)
{
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || c == 0x231A || c == 0x231B
           || c == 0x2328 || c == 0x23CF || (c >= 0x23E9 && c <= 0x23F3) || (c >= 0x23F8 && c <= 0x23FA)
           || (c >= 0x2B05 && c <= 0x2B07) || c == 0x2B1B || c == 0x2B1C || c == 0x2B50 || c == 0x2B55
           || c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299;
}

// Symbols that are only emoji when followed by the emoji presentation selector (arrows, (c), (r)...).
bool isTextDefaultSymbol(char32_t c)
{
    return c == 0x00A9 || c == 0x00AE || c == 0x203C || c == 0x2049 || c == 0x2122 || c == 0x2139
           || (c >= 0x2194 && c <= 0x21AA) || c == 0x24C2 || (c >= 0x25AA && c <= 0x25FE) || c == 0x2934
           || c == 0x2935;
}

bool isSkinTone(char32_t c)
{
    return c >= 0x1F3FB && c <= 0x1F3FF;
}

bool isEmojiComponent(char32_t c)
{
    return c == 0xFE0F || c == 0xFE0E || c == 0x20E3 || isSkinTone(c) || (c >= 0xE0020 && c <= 0xE007F);
}

bool isRegionalIndicator(char32_t c)
{
    return c >= 0x1F1E6 && c <= 0x1F1FF;
}

struct EmojiCluster
{
    qsizetype end = 0;
    QList<char32_t> elements; // base code points, joined by ZWJ in the text
    char32_t keycap = 0;      // keycap "1" -> '1'
    bool flag = false;        // a pair of regional indicators
};

// Reads one emoji (with its modifiers and ZWJ continuations) starting at `i`.
bool readEmoji(const QList<uint> &cps, qsizetype i, EmojiCluster &out)
{
    const auto at = [&cps](qsizetype k) -> char32_t { return k < cps.size() ? char32_t(cps.at(k)) : 0; };
    const char32_t first = at(i);
    if ((first >= '0' && first <= '9') || first == '#' || first == '*') {
        qsizetype k = i + 1;
        if (at(k) == 0xFE0F)
            ++k;
        if (at(k) != 0x20E3)
            return false;
        out.keycap = first;
        out.end = k + 1;
        return true;
    }
    if ((!isPictographic(first) || isSkinTone(first)) && !(isTextDefaultSymbol(first) && at(i + 1) == 0xFE0F))
        return false;
    qsizetype k = i;
    for (;;) {
        const char32_t base = at(k++);
        out.elements.append(base);
        if (isRegionalIndicator(base) && isRegionalIndicator(at(k))) {
            ++k;
            out.flag = true;
        }
        while (isEmojiComponent(at(k)))
            ++k;
        // A zero-width joiner continues the emoji with the next pictograph or symbol.
        const char32_t next = at(k + 1);
        const bool joinable = isPictographic(next) || (next >= 0x2000 && next <= 0x2BFF);
        if (at(k) == 0x200D && joinable && !isEmojiComponent(next)) {
            ++k;
            continue;
        }
        break;
    }
    out.end = k;
    return true;
}

QString emojiName(const EmojiCluster &cluster)
{
    const auto single = [](char32_t c) -> QString {
        const char *name = emojiNames().value(c);
        return name ? QCoreApplication::translate("Emoji", name) : QString();
    };
    if (cluster.flag && cluster.elements.size() == 1)
        return QCoreApplication::translate("Emoji", "flag");
    if (cluster.elements.size() == 1)
        return single(cluster.elements.first());
    const char *name = emojiSequences().value(toString(cluster.elements.constData(), cluster.elements.size()));
    if (name)
        return QCoreApplication::translate("Emoji", name);
    // Read the parts of other sequences: family "man woman girl", "person running" -> "running".
    QStringList parts;
    for (const char32_t c : cluster.elements) {
        const QString part = single(c);
        if (!part.isEmpty() && (parts.isEmpty() || parts.last() != part))
            parts << part;
    }
    return parts.join(QLatin1Char(' '));
}

void appendCodePoint(QString &out, char32_t c)
{
    if (QChar::requiresSurrogates(c)) {
        out += QChar(QChar::highSurrogate(c));
        out += QChar(QChar::lowSurrogate(c));
    } else {
        out += QChar(char16_t(c));
    }
}

bool isClosingPunctuation(char32_t c)
{
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || c == ')' || c == ']';
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

} // namespace

namespace TextProcessor {

QString expandReplacements(const QString &text, const QVariantMap &replacements)
{
    if (replacements.isEmpty() || text.isEmpty())
        return text;

    // Tokenise into word / non-word runs so punctuation stays where it was.
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}\\p{N}_']+"));
    QHash<QString, QString> lookup;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!key.isEmpty())
            lookup.insert(key, it.value().toString());
    }

    QString out;
    out.reserve(text.size() + 16);
    qsizetype last = 0;
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString word = m.captured();
        const auto found = lookup.constFind(word.toLower());
        if (found == lookup.cend()) {
            out += word;
        } else {
            QString expansion = found.value();
            if (!expansion.isEmpty() && word.at(0).isUpper() && expansion.at(0).isLower())
                expansion[0] = expansion.at(0).toUpper();
            out += expansion;
        }
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QString normalizeForSpeech(const QString &text)
{
    QString s;
    s.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t'))
            s += QLatin1Char(' ');
        else if (c.category() == QChar::Other_Control)
            continue;
        else
            s += c;
    }
    s = s.simplified();
    static const QRegularExpression speakable(QStringLiteral("[\\p{L}\\p{N}]"));
    if (!speakable.match(s).hasMatch())
        return QString();
    return s;
}

QStringList splitForSpeech(const QString &input, int minChars, int maxChars)
{
    const QString text = normalizeForSpeech(input);
    if (text.isEmpty())
        return {};
    if (text.size() <= maxChars && text.size() <= minChars * 3)
        return {text};

    // 1. Sentence boundaries: terminal punctuation followed by whitespace.
    static const QRegularExpression boundary(QStringLiteral("(?<=[.!?…。！？])\\s+"));
    const QStringList sentences = text.split(boundary, Qt::SkipEmptyParts);

    // 2. Break over-long sentences at commas/semicolons, then at spaces.
    QStringList pieces;
    for (const QString &sentence : sentences) {
        if (sentence.size() <= maxChars) {
            pieces << sentence;
            continue;
        }
        static const QRegularExpression soft(QStringLiteral("(?<=[,;:—])\\s+"));
        QString current;
        const QStringList clauses = sentence.split(soft, Qt::SkipEmptyParts);
        for (const QString &clause : clauses) {
            const QStringList words = clause.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &word : words) {
                if (!current.isEmpty() && current.size() + 1 + word.size() > maxChars) {
                    pieces << current;
                    current.clear();
                }
                current += current.isEmpty() ? word : QLatin1Char(' ') + word;
            }
            // Prefer a break at the end of a clause if the chunk is already long enough.
            if (current.size() >= minChars) {
                pieces << current;
                current.clear();
            }
        }
        if (!current.isEmpty())
            pieces << current;
    }

    // 3. Merge tiny pieces with their neighbour so prosody doesn't get choppy.
    QStringList merged;
    for (const QString &piece : pieces) {
        if (!merged.isEmpty()
            && (merged.last().size() < minChars || piece.size() < minChars / 2)
            && merged.last().size() + 1 + piece.size() <= maxChars) {
            merged.last() += QLatin1Char(' ') + piece;
        } else {
            merged << piece;
        }
    }
    return merged;
}

QString cleanTranscript(const QString &text)
{
    QString s = text;
    static const QRegularExpression bracketed(QStringLiteral("\\[[^\\]]*\\]|\\([^\\)]*\\)|\\*[^*]*\\*"));
    s.remove(bracketed);
    static const QRegularExpression music(QStringLiteral("[♪♫♬♭♮♯]"));
    s.remove(music);
    s = s.simplified();
    // Whisper sometimes emits a lone punctuation mark or "-" for silence.
    static const QRegularExpression letters(QStringLiteral("[\\p{L}\\p{N}]"));
    if (!letters.match(s).hasMatch())
        return QString();
    return s;
}

QStringList vocabularyFrom(const QString &text)
{
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}']{3,}"));
    QStringList words;
    auto it = wordRe.globalMatch(text);
    while (it.hasNext())
        words << it.next().captured().toLower();
    return words;
}

QString expandVariables(const QString &text, const VariableContext &context)
{
    if (!text.contains(QLatin1Char('{')) && !text.contains(QLatin1Char('}')))
        return text;

    const QDateTime now = context.now.isValid() ? context.now : QDateTime::currentDateTime();
    QHash<QString, QString> custom;
    for (auto it = context.custom.cbegin(); it != context.custom.cend(); ++it)
        custom.insert(it.key().trimmed().toLower(), it.value().toString());

    const auto lookup = [&](const QString &token, QString *value) {
        const QString name = token.trimmed().toLower();
        const auto found = custom.constFind(name);
        if (found != custom.cend())
            *value = found.value();
        else if (name == QLatin1String("time"))
            *value = QLocale().toString(now.time(), QLocale::ShortFormat);
        else if (name == QLatin1String("date"))
            *value = QLocale().toString(now.date(), QLocale::LongFormat);
        else if (name == QLatin1String("day"))
            *value = QLocale().dayName(now.date().dayOfWeek(), QLocale::LongFormat);
        else if (name == QLatin1String("clipboard"))
            *value = context.clipboard.trimmed();
        else if (name == QLatin1String("voice"))
            *value = context.voiceName;
        else
            return false;
        return true;
    };

    QString out;
    out.reserve(text.size() + 32);
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        if ((c == QLatin1Char('{') || c == QLatin1Char('}')) && next == c) {
            out += c; // "{{" and "}}" are literal braces
            i += 2;
            continue;
        }
        if (c == QLatin1Char('{')) {
            const qsizetype close = text.indexOf(QLatin1Char('}'), i + 1);
            const QString token = close > i ? text.mid(i + 1, close - i - 1) : QString();
            QString value;
            if (close > i && !token.contains(QLatin1Char('{')) && lookup(token, &value)) {
                out += value;
                i = close + 1;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

QString handleEmoji(const QString &text, EmojiMode mode)
{
    if (mode == EmojiMode::Keep || text.isEmpty())
        return text;

    const QList<uint> cps = text.toUcs4();
    QString out;
    out.reserve(text.size());
    bool touched = false;
    bool spaceAfter = false; // a spoken name needs a space before the next word
    bool removedGap = false; // something was removed right before this point
    QString lastName;        // collapses repeats into one word
    qsizetype i = 0;
    while (i < cps.size()) {
        const char32_t c = char32_t(cps.at(i));
        EmojiCluster cluster;
        if (readEmoji(cps, i, cluster)) {
            touched = true;
            i = cluster.end;
            if (cluster.keycap) {
                if (spaceAfter)
                    out += QLatin1Char(' ');
                appendCodePoint(out, cluster.keycap);
                spaceAfter = removedGap = false;
                lastName.clear();
                continue;
            }
            const QString name = mode == EmojiMode::Speak ? emojiName(cluster) : QString();
            if (name.isEmpty()) {
                removedGap = true;
                continue;
            }
            if (name == lastName)
                continue;
            if (!out.isEmpty() && !out.at(out.size() - 1).isSpace())
                out += QLatin1Char(' ');
            out += name;
            lastName = name;
            spaceAfter = true;
            removedGap = false;
            continue;
        }
        if (isEmojiComponent(c)) { // stray skin tone or variation selector
            touched = true;
            ++i;
            continue;
        }
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (spaceAfter && !space && !isClosingPunctuation(c))
            out += QLatin1Char(' ');
        if (removedGap && isClosingPunctuation(c)) {
            if (out.endsWith(QLatin1Char(' ')))
                out.chop(1); // "great <emoji>!" -> "great!"
        } else if (removedGap && !space && !out.isEmpty() && !out.at(out.size() - 1).isSpace()) {
            out += QLatin1Char(' '); // "hi<emoji>there" -> "hi there"
        }
        if (!space) {
            lastName.clear();
            spaceAfter = false;
            removedGap = false;
        }
        appendCodePoint(out, c);
        ++i;
    }
    if (!touched)
        return text;
    static const QRegularExpression gaps(QStringLiteral("[ \\t]{2,}"));
    out.replace(gaps, QStringLiteral(" "));
    return out.trimmed();
}

QString handleUrls(const QString &text, UrlMode mode)
{
    if (mode == UrlMode::Keep || text.isEmpty())
        return text;
    static const QRegularExpression urlRe(QStringLiteral("(?<![\\w@./])(?:https?://|www\\.)[^\\s<>\"]+"),
                                          QRegularExpression::CaseInsensitiveOption);
    const QString replacement = mode == UrlMode::SayLink ? QCoreApplication::translate("TextProcessor", "link")
                                                         : QString();
    QString out;
    qsizetype last = 0;
    bool found = false;
    auto it = urlRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        qsizetype start = m.capturedStart();
        qsizetype end = m.capturedEnd();
        // Sentence punctuation and unbalanced closing brackets belong to the text, not the link.
        while (end > start) {
            const QChar c = text.at(end - 1);
            const QString url = text.mid(start, end - start);
            if (QStringLiteral(".,;:!?'*").contains(c)
                || (c == QLatin1Char(')') && url.count(QLatin1Char('(')) < url.count(QLatin1Char(')')))
                || (c == QLatin1Char(']') && url.count(QLatin1Char('[')) < url.count(QLatin1Char(']'))))
                --end;
            else
                break;
        }
        if (end - start < 5)
            continue;
        if (mode == UrlMode::Remove && start > 0 && end < text.size()) {
            const QChar open = text.at(start - 1);
            const QChar close = text.at(end);
            if ((open == QLatin1Char('(') && close == QLatin1Char(')'))
                || (open == QLatin1Char('<') && close == QLatin1Char('>'))
                || (open == QLatin1Char('[') && close == QLatin1Char(']'))) {
                --start;
                ++end;
            }
        }
        out += text.mid(last, start - last);
        out += replacement;
        last = end;
        found = true;
    }
    if (!found)
        return text;
    out += text.mid(last);
    if (mode == UrlMode::Remove) {
        static const QRegularExpression gaps(QStringLiteral("[ \\t]{2,}"));
        static const QRegularExpression beforePunctuation(QStringLiteral(" +(?=[.,!?;:])"));
        out.replace(gaps, QStringLiteral(" "));
        out.replace(beforePunctuation, QString());
        out = out.trimmed();
    }
    return out;
}

QString autoCapitalize(const QString &text)
{
    static const QStringList abbreviations = {
        QStringLiteral("e.g"), QStringLiteral("i.e"), QStringLiteral("vs"),  QStringLiteral("mr"),
        QStringLiteral("mrs"), QStringLiteral("ms"),  QStringLiteral("dr"),  QStringLiteral("prof"),
        QStringLiteral("st"),  QStringLiteral("jr"),  QStringLiteral("sr"),  QStringLiteral("approx"),
    };
    const auto isWordChar = [](QChar c) { return c.isLetterOrNumber() || c.isMark() || isApostrophe(c); };

    QString s = text;
    bool sentenceStart = true;
    qsizetype i = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        if (c.isLetterOrNumber()) {
            qsizetype end = i;
            while (end < s.size() && isWordChar(s.at(end)))
                ++end;
            const qsizetype wordEnd = end;
            if (sentenceStart && c.isLower()) {
                s[i] = c.toUpper();
            } else if (c == QLatin1Char('i')) {
                // Standalone "i" and "i'm", "i'll"... but not "i.e."
                const bool alone = wordEnd == i + 1
                                   && !(wordEnd + 1 < s.size() && s.at(wordEnd) == QLatin1Char('.')
                                        && s.at(wordEnd + 1).isLetter());
                const bool contraction = wordEnd > i + 2 && isApostrophe(s.at(i + 1));
                if (alone || contraction)
                    s[i] = QLatin1Char('I');
            }
            sentenceStart = false;
            i = wordEnd;
            continue;
        }
        if (c == QLatin1Char('\n')) {
            sentenceStart = true;
        } else if (c == QLatin1Char('!') || c == QLatin1Char('?') || c == QLatin1Char('.')) {
            qsizetype after = i + 1;
            while (after < s.size() && QStringLiteral(u"\"')]\u201D\u2019").contains(s.at(after)))
                ++after;
            const bool followedBySpace = after >= s.size() || s.at(after).isSpace();
            bool ends = followedBySpace;
            if (ends && c == QLatin1Char('.')) {
                // Not an ellipsis ("so... yeah") or an abbreviation ("e.g. this").
                qsizetype w = i;
                while (w > 0 && !s.at(w - 1).isSpace() && s.at(w - 1) != QLatin1Char('('))
                    --w;
                const QString before = s.mid(w, i - w).toLower();
                ends = !(i > 0 && s.at(i - 1) == QLatin1Char('.')) && !abbreviations.contains(before);
            }
            if (ends)
                sentenceStart = true;
        } else if (c == QChar(0x2026)) {
            sentenceStart = false;
        }
        ++i;
    }
    return s;
}

} // namespace TextProcessor
