#include "osup_beatmap.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define PRINT(...)       \
  do {                   \
    printf(__VA_ARGS__); \
    fflush(stdout);      \
  } while (0);

typedef enum {
  /* key-value sections */
  OSUP_BM_SECTION_GENERAL,
  OSUP_BM_SECTION_EDITOR,
  OSUP_BM_SECTION_METADATA,
  OSUP_BM_SECTION_DIFFICULTY,
  OSUP_BM_SECTION_COLORS,

  /* comma-separated value sections */
  OSUP_BM_SECTION_EVENTS,
  OSUP_BM_SECTION_TIMING_POINTS,
  OSUP_BM_SECTION_HIT_OBJECTS
} osup_bm_section;

typedef struct {
  char version[16]; /* should be more than enough to store the version */
  osup_bm_section section;
  osup_bm* map;

  /* we will need to add events to the event list, but since we don't count the
   * number of events, we must add them to list dynamically, hence we need to
   * store the capacity */
  size_t eventCapacity;
  /* same for timing points, colors, and hit objects */
  size_t timingpointCapacity;
  size_t colorComboCapacity;
  size_t hitObjectCapacity;

} osup_bm_ctx;

static osup_bool osup_check_version(osup_bm_ctx* ctx) {
  /* only v14 is supported for the time being */
  return !strcmp(ctx->version, "14");
}

/* check if *string starts with prefix or not, if true, increase *string by
 * prefixLength */
static osup_bool osup_check_prefix_and_advance(const char** string,
                                               const char* prefix) {
  const char* it = *string;
  while (*prefix) {
    if (*(it++) != *(prefix++)) {
      return osup_false;
    }
  }
  *string = it;
  return osup_true;
}

/* advance line pointer to the first char of next line, or at the null
 * terminator if this is the last line, also check for non-blank character if
 * checkForNonBlankChar is true */
OSUP_INTERN osup_bool
osup_advance_to_next_line(const char** line, osup_bool checkForNonBlankChar) {
  while (osup_true) {
    if (**line == '\0') {
      return osup_true;
    } else if (**line == '\r' || **line == '\n') {
      ++(*line);
      return osup_true;
    } else if (checkForNonBlankChar && !isblank(**line)) {
      return osup_false;
    } else {
      ++(*line);
    }
  }
}

/* advance line pointer to AFTER the last non-blank character of line, and
 * return the line-terminating character (one of '\0', '\r' and '\n') */
OSUP_INTERN const char* osup_advance_to_last_nonblank_char(const char** line) {
  const char* it = *line;
  while (!osup_is_line_terminator(*it)) {
    if (isblank(*it)) {
      ++it;
    } else {
      *line = ++it;
    }
  }
  return it;
}

/*********************************************
 * HELPER MACROS FOR PARSING KEY-VALUE LINES *
 *********************************************/
#if 0
/* without macro version */
OSUP_INTERN osup_bool osup_bm_parse_general_line(osup_bm_ctx* ctx,
                                                 const char** line) {
  osup_bm_general* general = &ctx->map->general;
  const char* valueEnd;
  osup_int enumValue;
  if (osup_check_prefix_and_advance(line, "AudioFilename: ")) {
    /* initialize valueEnd (to prepare for the
     * osup_advance_to_last_nonblank_char call) */
    valueEnd = *line;
    /* store the current value of *line */
    const char* valueBegin = *line;
    /* advance *line to end-of-line, also getting valueEnd */
    *line = osup_advance_to_last_nonblank_char(&valueEnd);
    /* copy the string, return osup_true if success */
    return osup_strdup(&valueBegin, valueEnd, &general->audioFilename);
  }
  if (osup_check_prefix_and_advance(line, "AudioLeadIn: ")) {
    /* the same process */
    valueEnd = *line;
    const char* valueBegin = *line;
    *line = osup_advance_to_last_nonblank_char(&valueEnd);
    return osup_parse_int(&valueBegin, valueEnd, &general->audioLeadIn);
  }
  if (osup_check_prefix_and_advance(line, "Countdown: ")) {
    valueEnd = *line;
    const char* valueBegin = *line;
    *line = osup_advance_to_last_nonblank_char(&valueEnd);
    /* int enum parsing */
    if (!osup_parse_int(&valueBegin, valueEnd, &enumValue) ||
        enumValue < OSUP_COUNTDOWN_SPEED_NONE ||
        enumValue > OSUP_COUNTDOWN_SPEED_DOUBLE) {
      return osup_false;
    }
    general->countdown = enumValue;
  }
  if (osup_check_prefix_and_advance(line, "SampleSet: ")) {
    valueEnd = *line;
    const char* valueBegin = *line;
    *line = osup_advance_to_last_nonblank_char(&valueEnd);
    if (valueEnd - valueBegin == sizeof("Normal") - 1) {
      if (memcmp(valueBegin, "Normal", sizeof("Normal") - 1)) {
        general->sampleSet = OSUP_SAMPLESET_NORMAL;
        return osup_true;
      }
    }
    if (valueEnd - valueBegin == sizeof("Soft") - 1) {
      if (memcmp(valueBegin, "Soft", sizeof("Soft") - 1)) {
        general->sampleSet = OSUP_SAMPLESET_SOFT;
        return osup_true;
      }
    }
    if (valueEnd - valueBegin == sizeof("Drum") - 1) {
      if (memcmp(valueBegin, "Drum", sizeof("Drum") - 1)) {
        general->sampleSet = OSUP_SAMPLESET_DRUM;
        return osup_true;
      }
    }
    return osup_false;
  }
  /* any many other properties */
  return osup_false;
}
#endif

/* advance valueEnd pointer to the end of value */
#define OSUP_BM_KV_GET_VALUE()    \
  const char* valueEnd = *line;   \
  const char* valueBegin = *line; \
  *line = osup_advance_to_last_nonblank_char(&valueEnd)

/* parse macros */
#define OSUP_BM_KV_PARSE_STRING(prefix, member)                   \
  if (osup_check_prefix_and_advance(line, prefix)) {              \
    OSUP_BM_KV_GET_VALUE();                                       \
    return osup_strdup(&valueBegin, valueEnd, &ctx->map->member); \
  }

#define OSUP_BM_KV_PARSE_INT(prefix, member)                         \
  if (osup_check_prefix_and_advance(line, prefix)) {                 \
    OSUP_BM_KV_GET_VALUE();                                          \
    return osup_parse_int(&valueBegin, valueEnd, &ctx->map->member); \
  }

#define OSUP_BM_KV_PARSE_BOOL(prefix, member)                         \
  if (osup_check_prefix_and_advance(line, prefix)) {                  \
    OSUP_BM_KV_GET_VALUE();                                           \
    return osup_parse_bool(&valueBegin, valueEnd, &ctx->map->member); \
  }

#define OSUP_BM_KV_PARSE_DECIMAL(prefix, member)                         \
  if (osup_check_prefix_and_advance(line, prefix)) {                     \
    OSUP_BM_KV_GET_VALUE();                                              \
    return osup_parse_decimal(&valueBegin, valueEnd, &ctx->map->member); \
  }
#define OSUP_BM_KV_PARSE_RGB(prefix, member)                         \
  if (osup_check_prefix_and_advance(line, prefix)) {                 \
    OSUP_BM_KV_GET_VALUE();                                          \
    return osup_parse_rgb(&valueBegin, valueEnd, &ctx->map->member); \
  }

#define OSUP_BM_KV_PARSE_INT_ENUM(prefix, member, minEnum, maxEnum) \
  if (osup_check_prefix_and_advance(line, prefix)) {                \
    OSUP_BM_KV_GET_VALUE();                                         \
    osup_int enumValue;                                             \
    if (!osup_parse_int(&valueBegin, valueEnd, &enumValue) ||       \
        enumValue < minEnum || enumValue > maxEnum) {               \
      return osup_false;                                            \
    }                                                               \
    ctx->map->member = enumValue;                                   \
    return osup_true;                                               \
  }
/* for string-based enum, we have to manually process it
 * but there still is a helper macro */
#define OSUP_BM_KV_CHECK_STRING_ENUM(member, enumString, value)    \
  if (valueEnd - valueBegin == sizeof(enumString) - 1) {           \
    if (!memcmp(valueBegin, enumString, sizeof(enumString) - 1)) { \
      ctx->map->member = value;                                    \
      return osup_true;                                            \
    }                                                              \
  }

OSUP_INTERN osup_bool osup_bm_parse_general_line(osup_bm_ctx* ctx,
                                                 const char** line) {
  const char* valueEnd;
  osup_int enumValue;
  OSUP_BM_KV_PARSE_STRING("AudioFilename: ", general.audioFilename);
  OSUP_BM_KV_PARSE_INT("AudioLeadIn: ", general.audioLeadIn);
  OSUP_BM_KV_PARSE_STRING("AudioHash: ", general.audioHash);
  OSUP_BM_KV_PARSE_INT("PreviewTime: ", general.previewTime);
  OSUP_BM_KV_PARSE_INT_ENUM("Countdown", general.countdown,
                            OSUP_COUNTDOWN_SPEED_NONE,
                            OSUP_COUNTDOWN_SPEED_DOUBLE);
  OSUP_BM_KV_PARSE_DECIMAL("StackLeniency: ", general.stackLeniency);
  OSUP_BM_KV_PARSE_INT_ENUM("Mode: ", general.mode, OSUP_MODE_OSU,
                            OSUP_MODE_MANIA);
  OSUP_BM_KV_PARSE_BOOL("LetterboxInBreaks: ", general.letterboxInBreaks);
  OSUP_BM_KV_PARSE_BOOL("StoryFireInFront: ", general.storyFireInFront);
  OSUP_BM_KV_PARSE_BOOL("UseSkinSprites: ", general.useSkinSprites);
  OSUP_BM_KV_PARSE_BOOL("AlwaysShowPlayfield: ", general.alwaysShowPlayfield);
  OSUP_BM_KV_PARSE_STRING("SkinPreference: ", general.skinPreference);
  OSUP_BM_KV_PARSE_BOOL("EpilepsyWarning: ", general.epilepsyWarning);
  OSUP_BM_KV_PARSE_INT("CountdownOffset: ", general.countdownOffset);
  OSUP_BM_KV_PARSE_BOOL("SpecialStyle: ", general.specialStyle);
  OSUP_BM_KV_PARSE_BOOL("WidescreenStoryboard: ", general.widescreenStoryboard);
  OSUP_BM_KV_PARSE_BOOL("SamplesMatchPlaybackRate",
                        general.samplesMatchPlaybackRate);

  if (osup_check_prefix_and_advance(line, "SampleSet: ")) {
    OSUP_BM_KV_GET_VALUE();
    OSUP_BM_KV_CHECK_STRING_ENUM(general.sampleSet, "Normal",
                                 OSUP_SAMPLESET_NORMAL);
    OSUP_BM_KV_CHECK_STRING_ENUM(general.sampleSet, "Soft",
                                 OSUP_SAMPLESET_SOFT);
    OSUP_BM_KV_CHECK_STRING_ENUM(general.sampleSet, "Drum",
                                 OSUP_SAMPLESET_DRUM);
    return osup_false;
  }

  if (osup_check_prefix_and_advance(line, "OverlayPosition: ")) {
    OSUP_BM_KV_GET_VALUE();
    OSUP_BM_KV_CHECK_STRING_ENUM(general.overlayPosition, "NoChange",
                                 OSUP_OVERLAYPOS_NOCHANGE);
    OSUP_BM_KV_CHECK_STRING_ENUM(general.overlayPosition, "Below",
                                 OSUP_OVERLAYPOS_BELOW);
    OSUP_BM_KV_CHECK_STRING_ENUM(general.overlayPosition, "Above",
                                 OSUP_OVERLAYPOS_ABOVE);
    return osup_false;
  }

  return osup_false;
}

OSUP_INTERN osup_bool osup_bm_parse_editor_line(osup_bm_ctx* ctx,
                                                const char** line) {
  OSUP_BM_KV_PARSE_DECIMAL("DistanceSpacing: ", editor.distanceSpacing);
  OSUP_BM_KV_PARSE_DECIMAL("BeatDivisor: ", editor.beatDivisor);
  OSUP_BM_KV_PARSE_DECIMAL("TimelineZoom: ", editor.timelineZoom);
  OSUP_BM_KV_PARSE_INT("GridSize: ", editor.gridSize);

  if (osup_check_prefix_and_advance(line, "Bookmarks: ")) {
    OSUP_BM_KV_GET_VALUE();
    /* this is a comma-separated list of int */
    size_t elementCount = 1;
    /* the number of elements = the number of delimiters + 1 */
    {
      const char* it = valueBegin;
      while (it < valueEnd) {
        if (*(it++) == ',') elementCount++;
      }
    }
    ctx->map->editor.bookmarks.elements =
        malloc(elementCount * sizeof(osup_int));
    ctx->map->editor.bookmarks.count = elementCount;
    size_t index = 0;
    const char* elementBegin = NULL;
    const char* elementEnd = valueBegin - 1;
    while (osup_split_string(',', &elementBegin, &elementEnd, valueEnd)) {
      if (!osup_parse_int(&elementBegin, elementEnd,
                          &ctx->map->editor.bookmarks.elements[index])) {
        return osup_false;
      }
    }
    return osup_true;
  }

  return osup_false;
}

OSUP_INTERN osup_bool osup_bm_parse_metadata_line(osup_bm_ctx* ctx,
                                                  const char** line) {
  OSUP_BM_KV_PARSE_STRING("Title:", metadata.title);
  OSUP_BM_KV_PARSE_STRING("TitleUnicode:", metadata.titleUnicode);
  OSUP_BM_KV_PARSE_STRING("Artist:", metadata.artist);
  OSUP_BM_KV_PARSE_STRING("ArtistUnicode:", metadata.artistUnicode);
  OSUP_BM_KV_PARSE_STRING("Creator:", metadata.creator);
  OSUP_BM_KV_PARSE_STRING("Version:", metadata.version);
  OSUP_BM_KV_PARSE_STRING("Source:", metadata.source);
  OSUP_BM_KV_PARSE_STRING("Title:", metadata.title);
  OSUP_BM_KV_PARSE_INT("BeatmapID:", metadata.beatmapID);
  OSUP_BM_KV_PARSE_INT("BeatmapSetID:", metadata.beatmapSetID);

  if (osup_check_prefix_and_advance(line, "Tags:")) {
    OSUP_BM_KV_GET_VALUE();
    char* tags;
    char* tagsEnd = valueEnd - valueBegin + tags;
    if (!osup_strdup(&valueBegin, valueEnd, &tags)) {
      return osup_false;
    }
    /* since beatmap tags is a space-separated list of strings, we can just
     * replace all space with '\0' and we got a bunch of null-terminated strings
     */
    char* it = tags;
    size_t tagCount = 1;
    while (*it) {
      if (*it == ' ') {
        *it = '\0';
        tagCount++;
      }
      ++it;
    }

    /* allocating memory */
    ctx->map->metadata.tags.elements = malloc(tagCount * sizeof(char*));
    ctx->map->metadata.tags.count = tagCount;
    if (!ctx->map->metadata.tags.elements) {
      return osup_false;
    }

    /* re-iterating to populated allocated memory  */
    it = tags;
    size_t index = 0;
    while (index < tagCount) {
      /* add pointers to first character to list */
      ctx->map->metadata.tags.elements[index++] = it;
      /* skip all non-'\0' characters */
      while (*it) ++it;
      /* go to the first character of next token */
      ++it;
    }

    return osup_true;
  }

  return osup_false;
}

OSUP_INTERN osup_bool osup_bm_parse_difficulty_line(osup_bm_ctx* ctx,
                                                    const char** line) {
  OSUP_BM_KV_PARSE_DECIMAL("HPDrainRate:", difficulty.hpDrainRate);
  OSUP_BM_KV_PARSE_DECIMAL("CircleSize:", difficulty.circleSize);
  OSUP_BM_KV_PARSE_DECIMAL("OverallDifficulty:", difficulty.overallDifficulty);
  OSUP_BM_KV_PARSE_DECIMAL("ApproachRate:", difficulty.approachRate);
  OSUP_BM_KV_PARSE_DECIMAL("SliderMultiplier:", difficulty.sliderMultiplier);
  OSUP_BM_KV_PARSE_DECIMAL("SliderTickRate:", difficulty.sliderTickRate);
  return osup_false;
}

OSUP_INTERN osup_bool osup_bm_parse_events_line(osup_bm_ctx* ctx,
                                                const char** line,
                                                osup_event* event) {
  /* this line is a comma-separated line, so we need to split and process it */
  const char* elementBegin = NULL;
  const char* elementEnd = *line - 1;

  /* get first token from line */
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd)) {
    return osup_false;
  }

  switch (elementEnd - elementBegin) {
    case 1: /* 0, 1 or 2 */
      switch (*elementBegin) {
        case '0':
          event->eventType = OSUP_EVENT_TYPE_BACKGROUND;
          break;
        case '1':
          event->eventType = OSUP_EVENT_TYPE_VIDEO;
          break;
        case '2':
          event->eventType = OSUP_EVENT_TYPE_BREAK;
          break;
        default:
          return osup_false;
      }
      break;
    case 5: /* Video or Break */
      if (!memcmp(elementBegin, "Video", sizeof("Video") - 1)) {
        event->eventType = OSUP_EVENT_TYPE_VIDEO;
        break;
      } else if (!memcmp(elementBegin, "Break", sizeof("Break") - 1)) {
        event->eventType = OSUP_EVENT_TYPE_BREAK;
        break;
      } else {
        return osup_false;
      }
    default:
      return osup_false;
  }

  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &event->startTime)) {
    return osup_false;
  };

  switch (event->eventType) {
    /* exact same structure */
    case OSUP_EVENT_TYPE_BACKGROUND:
    case OSUP_EVENT_TYPE_VIDEO:
      if (!osup_split_string_line_terminated_quoted(',', &elementBegin,
                                                    &elementEnd) ||
          !osup_strdup(&elementBegin, elementEnd, &event->bg.filename) ||
          !osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
          !osup_parse_int(&elementBegin, elementEnd, &event->bg.xOffset) ||
          !osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
          !osup_parse_int(&elementBegin, elementEnd, &event->bg.yOffset)) {
        return osup_false;
      }
      /* there should be no leftover tokens */
      *line = elementEnd;
      return osup_advance_to_next_line(line, osup_true);
    case OSUP_EVENT_TYPE_BREAK:

      if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
          !osup_parse_int(&elementBegin, elementEnd, &event->brk.endTime)) {
        return osup_false;
      };
      /* there should be no leftover tokens */
      *line = elementEnd;
      return osup_advance_to_next_line(line, osup_true);
  };
}

OSUP_INTERN osup_bool osup_bm_parse_timing_points_line(
    osup_bm_ctx* ctx, const char** line, osup_timingpoint* timingpoint) {
  const char* elementBegin = NULL;
  const char* elementEnd = *line - 1;
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &timingpoint->time)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_decimal(&elementBegin, elementEnd,
                          &timingpoint->beatLength)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &timingpoint->meter)) {
    return osup_false;
  }
  osup_int sampleSetValue;
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &sampleSetValue)) {
    return osup_false;
  }
  if (sampleSetValue >= OSUP_SAMPLESET_DEFAULT &&
      sampleSetValue <= OSUP_SAMPLESET_DRUM) {
    timingpoint->sampleSet = sampleSetValue;
  } else {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &timingpoint->sampleIndex)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &timingpoint->volume)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_bool(&elementBegin, elementEnd, &timingpoint->uninherited)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_ubyte(&elementBegin, elementEnd, &timingpoint->effects)) {
    return osup_false;
  }
  *line = elementEnd;
  return osup_advance_to_next_line(line, osup_true);
}

OSUP_INTERN osup_bool osup_bm_parse_colors_line(osup_bm_ctx* ctx,
                                                const char** line) {
  OSUP_BM_KV_PARSE_RGB("SliderTrackOverride : ", colors.sliderTrackOverride);
  OSUP_BM_KV_PARSE_RGB("SliderBorder : ", colors.sliderBorder);
  if (osup_check_prefix_and_advance(line, "Combo")) {
    size_t combo = 0;
    if (!isdigit(**line)) {
      return osup_false;
    }
    while (isdigit(**line)) {
      combo = combo * 10 + (**line - '0');
      ++(*line);
    }
    if (combo > sizeof(ctx->map->colors.combos) / sizeof(osup_rgb) ||
        combo == 0) {
      return osup_false;
    }
    osup_rgb value;
    if (!osup_check_prefix_and_advance(line, " : ")) {
      return osup_false;
    }

    OSUP_BM_KV_GET_VALUE();
    osup_parse_rgb(&valueBegin, valueEnd, &value);
    ctx->map->colors.combos[combo - 1] = value;

    return osup_true;
  }

  return osup_false;
}

OSUP_INTERN osup_bool osup_bm_parse_hit_objects_line(osup_bm_ctx* ctx,
                                                     const char** line,
                                                     osup_hitobject* value) {
  const char* elementBegin = NULL;
  const char* elementEnd = *line - 1;
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &value->x)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &value->y)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_int(&elementBegin, elementEnd, &value->time)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_ubyte(&elementBegin, elementEnd, &value->type)) {
    return osup_false;
  }
  if (!osup_split_string_line_terminated(',', &elementBegin, &elementEnd) ||
      !osup_parse_ubyte(&elementBegin, elementEnd, &value->hitSound)) {
    return osup_false;
  }
  *line = elementEnd;
  /* if you can't understand basic C, *((*line)++) means get the current
   * character *line is pointing to, and also increment *line by 1 */
  if (*((*line)++) != ',') return osup_false;

  /* if this sum is not 1, this hit object belongs to >= 2 or no types, which is
   * invalid */
  if ((value->type >> 0 & 1) + (value->type >> 1 & 1) + (value->type >> 3 & 1) +
          (value->type >> 7 & 1) !=
      1) {
    return osup_false;
  }
  /* hit circles have no objectParams */
  if (OSUP_IS_SLIDER(value->type)) {
    char curveTypeChar = *((*line)++);
    switch (curveTypeChar) {
      case 'B':
        value->slider.curveType = OSUP_CURVE_BEZIER;
        break;
      case 'C':
        value->slider.curveType = OSUP_CURVE_CENTRIPETAL_CATMULL_ROM;
        break;
      case 'L':
        value->slider.curveType = OSUP_CURVE_LINEAR;
        break;
      case 'P':
        value->slider.curveType = OSUP_CURVE_PERFECT_CIRCLE;
        break;
      default:
        return osup_false;
    }
    if (*((*line)++) != '|') {
      return osup_false;
    }

    const char* it = *line;
    size_t curvePointCount = 1;
    while (*it != ',' && !osup_is_line_terminator(*it)) {
      if (*it == '|') {
        curvePointCount++;
      }
      it++;
    }

    value->slider.curvePoints.elements =
        malloc(curvePointCount * sizeof(osup_vec2));
    if (!value->slider.curvePoints.elements) {
      return osup_false;
    }
    value->slider.curvePoints.count = curvePointCount;
    size_t index = 0;
    while (index < curvePointCount) {
      if (!osup_parse_int_until_nondigit_char(
              line, &value->slider.curvePoints.elements[index].x) ||
          *((*line)++) != ':' ||
          !osup_parse_int_until_nondigit_char(
              line, &value->slider.curvePoints.elements[index].y)) {
        return osup_false;
      }
      if (**line != ',' && **line != '|') {
        return osup_false;
      }
      ++(*line);
      ++index;
    }

    if (!osup_parse_int_until_nondigit_char(line, &value->slider.slides) ||
        *((*line)++) != ',') {
      return osup_false;
    }
    /* i'm too lazy to make a osup_parse_decimal_until_nondigit_char*/
    const char* valueEnd = *line;
    while (*valueEnd != ',') {
      if (osup_is_line_terminator(*valueEnd)) {
        return osup_false;
      }
      valueEnd++;
    }
    if (!osup_parse_decimal(line, valueEnd, &value->slider.length)) {
      return osup_false;
    }
    *line = valueEnd + 1;

    it = *line;
    size_t edgeSoundCount = 1;
    while (*it != ',') {
      if (*it == '|') {
        edgeSoundCount++;
      } else if (osup_is_line_terminator(*it)) {
        return osup_false;
      }
      it++;
    }

    value->slider.edgeSounds.elements =
        malloc(edgeSoundCount * sizeof(osup_int));
    if (!value->slider.edgeSounds.elements) {
      return osup_false;
    }
    value->slider.edgeSounds.count = edgeSoundCount;

    index = 0;
    while (index < edgeSoundCount) {
      if (!osup_parse_int_until_nondigit_char(
              line, &value->slider.edgeSounds.elements[index])) {
        return osup_false;
      }
      if (**line != ',' && **line != '|') {
        return osup_false;
      }
      ++(*line);
      ++index;
    }

    it = *line;
    size_t edgeSetCount = 1;
    while (*it != ',') {
      if (*it == '|') {
        edgeSetCount++;
      } else if (osup_is_line_terminator(*it)) {
        return osup_false;
      }
      it++;
    }

    value->slider.edgeSets.elements = malloc(edgeSetCount * sizeof(osup_vec2));
    if (!value->slider.edgeSets.elements) {
      return osup_false;
    }
    value->slider.edgeSets.count = edgeSetCount;
    index = 0;
    while (index < edgeSetCount) {
      if (!osup_parse_int_until_nondigit_char(
              line, &value->slider.edgeSets.elements[index].normalSet) ||
          *((*line)++) != ':' ||
          !osup_parse_int_until_nondigit_char(
              line, &value->slider.edgeSets.elements[index].additionSet)) {
        return osup_false;
      }
      if (**line != ',' && **line != '|') {
        return osup_false;
      }
      ++(*line);
      ++index;
    }
  } else if (OSUP_IS_SPINNER(value->type) || OSUP_IS_MANIA_HOLD(value->type)) {
    /* same structure, a little bit different syntax */
    if (!osup_parse_int_until_nondigit_char(line, &value->spinner.endTime)) {
      return osup_false;
    }

    if (OSUP_IS_SPINNER(value->type) ? **line != ',' : **line != ':') {
      return osup_false;
    }

    ++(*line);
  }

  osup_int i;
  if (!osup_parse_int_until_nondigit_char(line, &i) || *((*line)++) != ':' ||
      i < OSUP_SAMPLESET_DEFAULT || i > OSUP_SAMPLESET_DRUM) {
    return osup_false;
  }
  value->hitSample.normalSet = i;
  if (!osup_parse_int_until_nondigit_char(line, &i) || *((*line)++) != ':' ||
      i < OSUP_SAMPLESET_DEFAULT || i > OSUP_SAMPLESET_DRUM) {
    return osup_false;
  }
  value->hitSample.additionSet = i;
  if (!osup_parse_int_until_nondigit_char(line, &value->hitSample.index) ||
      *((*line)++) != ':') {
    return osup_false;
  }
  if (!osup_parse_int_until_nondigit_char(line, &value->hitSample.volume) ||
      *((*line)++) != ':') {
    return osup_false;
  }

  const char* filenameBegin = *line;
  const char* filenameEnd = *line;
  *line = osup_advance_to_last_nonblank_char(&filenameEnd);
  if (*filenameBegin == '"') {
    if (*filenameEnd != '"') {
      return osup_false;
    } else {
      ++filenameBegin;
      --filenameEnd;
      if (filenameBegin > filenameEnd) return osup_false;
      return osup_strdup(&filenameBegin, filenameEnd,
                         &value->hitSample.filename);
    }
  } else {
    return osup_strdup(&filenameBegin, filenameEnd, &value->hitSample.filename);
  }
}

/* will also advance the line pointer to the next line */
OSUP_INTERN osup_bool osup_bm_nextline(osup_bm_ctx* ctx, const char** line) {
  switch (**line) {
    case '/':
      /* probably a comment */
      if ((*line)[1] == '/') {
        /* yeah, it's a comment */
        return osup_advance_to_next_line(line, osup_false) /* always true */;
      } else {
        /* unexpected syntax */
        return osup_false;
      }
    case '\r':
    case '\n':
    case '\0':
      /* empty line */
      return osup_true;
    case '[':
      /* a section header */

      if (osup_check_prefix_and_advance(line, "[General]")) {
        ctx->section = OSUP_BM_SECTION_GENERAL;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[Editor]")) {
        ctx->section = OSUP_BM_SECTION_EDITOR;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[Metadata]")) {
        ctx->section = OSUP_BM_SECTION_METADATA;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[Difficulty]")) {
        ctx->section = OSUP_BM_SECTION_DIFFICULTY;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[Events]")) {
        ctx->section = OSUP_BM_SECTION_EVENTS;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[TimingPoints]")) {
        ctx->section = OSUP_BM_SECTION_TIMING_POINTS;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[Colours]")) {
        ctx->section = OSUP_BM_SECTION_COLORS;
        return osup_advance_to_next_line(line, osup_true);
      }
      if (osup_check_prefix_and_advance(line, "[HitObjects]")) {
        ctx->section = OSUP_BM_SECTION_HIT_OBJECTS;
        return osup_advance_to_next_line(line, osup_true);
      }

      return osup_false;
    default:
      switch (ctx->section) {
        case OSUP_BM_SECTION_GENERAL:
          return osup_bm_parse_general_line(ctx, line);
        case OSUP_BM_SECTION_EDITOR:
          return osup_bm_parse_editor_line(ctx, line);
        case OSUP_BM_SECTION_METADATA:
          return osup_bm_parse_metadata_line(ctx, line);
        case OSUP_BM_SECTION_DIFFICULTY:
          return osup_bm_parse_difficulty_line(ctx, line);
        case OSUP_BM_SECTION_EVENTS: {
          osup_bm_events* events = &ctx->map->events;
          if (events->count >= ctx->eventCapacity) {
            ctx->eventCapacity = (size_t)((events->count + 1) * 1.5);
            osup_event* newEvent = realloc(
                events->elements, ctx->eventCapacity * sizeof(osup_event));
            if (!newEvent) {
              return osup_false;
            }
            events->elements = newEvent;
          }
          osup_event* event = &events->elements[events->count];
          /* we won't parse storyboards (for the time being), so we won't throw
           * error for invalid effect type */
          if (osup_bm_parse_events_line(ctx, line, event)) {
            events->count++;
          }
          return osup_true;
        }

        case OSUP_BM_SECTION_TIMING_POINTS: {
          osup_bm_timingpoints* timingpoints = &ctx->map->timingPoints;
          if (timingpoints->count >= ctx->timingpointCapacity) {
            ctx->timingpointCapacity =
                (size_t)((timingpoints->count + 1) * 1.5);
            osup_timingpoint* newTimingPoints =
                realloc(timingpoints->elements,
                        ctx->timingpointCapacity * sizeof(osup_timingpoint));
            if (!newTimingPoints) {
              return osup_false;
            }
            timingpoints->elements = newTimingPoints;
          }
          osup_timingpoint* timingpoint =
              &timingpoints->elements[timingpoints->count];
          if (osup_bm_parse_timing_points_line(ctx, line, timingpoint)) {
            timingpoints->count++;
            return osup_true;
          } else {
            return osup_false;
          }
        }
        case OSUP_BM_SECTION_COLORS:
          return osup_bm_parse_colors_line(ctx, line);
        case OSUP_BM_SECTION_HIT_OBJECTS: {
          osup_bm_hitobjects* hitObjects = &ctx->map->hitObjects;
          if (hitObjects->count >= ctx->hitObjectCapacity) {
            ctx->hitObjectCapacity = (size_t)((hitObjects->count + 1) * 1.5);
            osup_hitobject* newHitObjects =
                realloc(hitObjects->elements,
                        ctx->hitObjectCapacity * sizeof(osup_hitobject));
            if (!newHitObjects) {
              return osup_false;
            }
            hitObjects->elements = newHitObjects;
          }
          osup_hitobject* hitObject = &hitObjects->elements[hitObjects->count];
          if (osup_bm_parse_hit_objects_line(ctx, line, hitObject)) {
            hitObjects->count++;
            return osup_true;
          } else {
            return osup_false;
          }
        }
      }
  }
}

OSUP_API osup_bool osup_beatmap_load(osup_bm* map, const char* file) {
  FILE* f = fopen(file, "r");
  if (!f) {
    return osup_false;
  }
  return osup_beatmap_load_stream(map, f);
}

OSUP_API osup_bool osup_beatmap_load_string(osup_bm* map, const char* string) {
  if (!strncmp(string, "osu file format v", sizeof("osu file format v") - 1)) {
    return osup_false;
  }

  osup_bm_ctx ctx = {};
  ctx.map = map;

  const char* versionBegin = string + sizeof("osu file format v") - 1;
  size_t i = 0;
  while (i < sizeof((osup_bm_ctx){}.version)) {
    ctx.version[i] = versionBegin[i];
    if (osup_is_line_terminator(ctx.version[i])) {
      ctx.version[i] = '\0';
      goto success;
    } else {
      i++;
    }
  }

  /* version has more than 16 chars, invalid */
  osup_error("invalid version");
  return osup_false;

success:
  if (!osup_check_version(&ctx)) {
    return osup_false;
  }

  /* go to the next line */
  const char* line = versionBegin + i;
  if (*line == '\0') {
    /* there is no line other than the header line, so this is an empty .osu
     * file, still technically correct input */
    return osup_false;
  }
  line++;

  do {
    /* parse line by line */
    if (!osup_bm_nextline(&ctx, &line)) {
      return osup_false;
    }
  } while (*line != '\0');

  return osup_true;
}

OSUP_API osup_bool osup_beatmap_load_callbacks(osup_bm* map,
                                               osup_bm_callback callback,
                                               void* ptr) {
  return osup_true;
}

OSUP_API osup_bool osup_beatmap_load_stream(osup_bm* map, FILE* file) {
  OSUP_STORAGE size_t defaultBufSize = 32;

  char header[sizeof("osu file format v") - 1];
  if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
      memcmp(header, "osu file format v", sizeof("osu file format v") - 1)) {
    return osup_false;
  }

  osup_bm_ctx ctx = {};
  ctx.map = map;

  size_t i = 0;
  while (i < sizeof(ctx.version)) {
    if (fread(&ctx.version[i], 1, 1, file) != -1) {
      if (osup_is_line_terminator(ctx.version[i])) {
        ctx.version[i] = '\0';
        goto success;
      } else {
        i++;
      }
    } else {
      return osup_false;
    }
  }
  /* no line terminator found, invalid version */
  return osup_false;

success:
  if (!osup_check_version(&ctx)) {
    return osup_false;
  }
  /* TODO: make this not depend on getline function */
  size_t bufSize = 32;
  char* line = malloc(defaultBufSize);
  while (getline(&line, &bufSize, file) != -1) {
    const char* lineConst = line;
    osup_bm_nextline(&ctx, &lineConst);
  }

  return osup_true;
}

