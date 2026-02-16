/**
 * Embedded Timezone Database Library.
 * Please do not modify the files in this library directly. They are auto generated
 * periodically from source to account for changes in the timezone definitions.
 * Instead the template files should be modified so that the changes are retained.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    /** Select if the short or full timezone listing should be included */
#define TZ_DB_USE_SHORT_LIST 1

#if TZ_DB_USE_SHORT_LIST
/** Number of time zones contained in this library */
#define TZ_DB_NUM_ZONES (140)
#else
#define TZ_DB_NUM_ZONES (427)

/** Select if links/aliases for historical zone names should be included */
#define TZ_DB_INCLUDE_ALIAS_LIST 1
#endif

#if TZ_DB_INCLUDE_ALIAS_LIST
#define TZ_DB_NUM_ALIAS (140)
#endif

/**
 * Timezone information structure.
 * Contains IANA Name (e.g. America/New_York)
 * And posix rule.
 **/
    typedef struct {
        const char* name;
        const char* rule;
    } embeddedTz_t;

    /**
     * Get the IANA database version used to create this file.
     * To check if this is the latest, see: https://www.iana.org/time-zones
     * @return             the library build version string
     **/
    const char* tz_db_get_version();

    /**
     * Looks up the a Timezone based on the supplied IANA timezone name.
     * Name strings are pre-hashed to make searching faster so this is the recommended
     * method for looking up zone information.
     * NOTE: If aliases are not included then only current timezone names are supported.
     * @param[in]   name   the tz database name for the timezone in question
     * @return             pointer to the timezone structure for the zone
     *                     NULL if not found.
     **/
    const embeddedTz_t* tz_db_getTimezone(const char* name);

    /**
     * Looks up the POSIX string corresponding to the given tz database name.
     * Name strings are pre-hashed to make searching faster so this is the recommended
     * method for looking up zone information.
     * NOTE: only current timezone names are supported. This database is limited.
     * @param[in]   name   the tz database name for the timezone in question
     * @return             the POSIX string for the timezone in question OR
     *                     NULL if not found.
     **/
    const char* tz_db_get_posix_str(const char* name);

    /**
     * List all zones for manual processing.
     * Allows the application to do things like displaying all available zones.
     * NOTE: Does not include alias list.
     * @return             Pointer to the first timezone in the array
     **/
    const embeddedTz_t* tz_db_get_all_zones();

#ifdef __cplusplus
}
#endif