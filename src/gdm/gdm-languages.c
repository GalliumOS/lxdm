/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2008  Red Hat, Inc,
 *           2007  William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Written by : William Jon McCann <mccann@jhu.edu>
 *              Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "gdm-languages.h"

#include <langinfo.h>
#ifndef __LC_LAST
#define __LC_LAST       13
#endif
#include "locarchive.h"

#define ALIASES_FILE LIBLOCALEDIR "/locale.alias"
#define ARCHIVE_FILE LIBLOCALEDIR "/locale-archive"
#define ISO_CODES_DATADIR ISO_CODES_PREFIX "/share/xml/iso-codes"
#define ISO_CODES_LOCALESDIR ISO_CODES_PREFIX "/share/locale"

typedef struct _GdmLocale {
        char *id;
        char *name;
        char *language_code;
        char *territory_code;
        char *codeset;
        char *modifier;
} GdmLocale;

static GHashTable *gdm_languages_map;
static GHashTable *gdm_territories_map;
static GHashTable *gdm_available_locales_map;

static void
gdm_locale_free (GdmLocale *locale)
{
        if (locale == NULL) {
                return;
        }

        g_free (locale->id);
        g_free (locale->name);
        g_free (locale->codeset);
        g_free (locale->modifier);
        g_free (locale);
}

static char *
normalize_codeset (const char *codeset)
{
        char *normalized_codeset;
        const char *p;
        char *q;

        normalized_codeset = g_strdup (codeset);

        if (codeset != NULL) {
                for (p = codeset, q = normalized_codeset;
                     *p != '\0'; p++) {

                        if (*p == '-' || *p == '_') {
                                continue;
                        }

                        *q = g_ascii_tolower (*p);
                        q++;
                }
                *q = '\0';
        }

        return normalized_codeset;
}

/*
 * According to http://en.wikipedia.org/wiki/Locale
 * locale names are of the form:
 * [language[_territory][.codeset][@modifier]]
 */
void
gdm_parse_language_name (const char *name,
                         char      **language_codep,
                         char      **territory_codep,
                         char      **codesetp,
                         char      **modifierp)
{
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;

        error = NULL;
        re = g_regex_new ("^(?P<language>[^_.@[:space:]]+)"
                          "(_(?P<territory>[[:upper:]]+))?"
                          "(\\.(?P<codeset>[-_0-9a-zA-Z]+))?"
                          "(@(?P<modifier>[[:ascii:]]+))?$",
                          0, 0, &error);
        if (re == NULL) {
                g_critical ("%s", error->message);
                return;
        }

        if (!g_regex_match (re, name, 0, &match_info) ||
            g_match_info_is_partial_match (match_info)) {
                g_match_info_free (match_info);
                g_regex_unref (re);
                g_warning ("locale %s isn't valid\n", name);
                return;
        }

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse locale: %s", name);
                return;
        }

        if (language_codep != NULL) {
                *language_codep = g_match_info_fetch_named (match_info, "language");
        }

        if (territory_codep != NULL) {
                *territory_codep = g_match_info_fetch_named (match_info, "territory");

                if (*territory_codep != NULL &&
                    *territory_codep[0] == '\0') {
                        g_free (*territory_codep);
                        *territory_codep = NULL;
                }
        }

        if (codesetp != NULL) {
                *codesetp = g_match_info_fetch_named (match_info, "codeset");

                if (*codesetp != NULL &&
                    *codesetp[0] == '\0') {
                        g_free (*codesetp);
                        *codesetp = NULL;
                }

                if (*codesetp != NULL) {
                        char *codeset;

                        codeset = normalize_codeset (*codesetp);
                        g_free (*codesetp);
                        *codesetp = codeset;
                }
        }

        if (modifierp != NULL) {
                *modifierp = g_match_info_fetch_named (match_info, "modifier");

                if (*modifierp != NULL &&
                    *modifierp[0] == '\0') {
                        g_free (*modifierp);
                        *modifierp = NULL;
                }
        }

        g_match_info_free (match_info);
        g_regex_unref (re);
}

static char *
construct_language_name (const char *language,
                         const char *territory,
                         const char *codeset,
                         const char *modifier)
{
        char *name;

        g_assert (language[0] != 0);
        g_assert (territory == NULL || territory[0] != 0);
        g_assert (codeset == NULL || codeset[0] != 0);
        g_assert (modifier == NULL || modifier[0] != 0);

        name = g_strdup_printf ("%s%s%s%s%s%s%s",
                                language,
                                territory != NULL? "_" : "",
                                territory != NULL? territory : "",
                                codeset != NULL? "." : "",
                                codeset != NULL? codeset : "",
                                modifier != NULL? "@" : "",
                                modifier != NULL? modifier : "");

        return name;
}

static void
make_codeset_canonical_for_locale (const char  *name,
                                   char       **codeset)
{
        char *old_locale;

        old_locale = setlocale (LC_CTYPE, name);

        if (old_locale == NULL) {
                return;
        }

        g_free (*codeset);
        *codeset = g_strdup (nl_langinfo (CODESET));

        setlocale (LC_CTYPE, old_locale);
}

char *
gdm_normalize_language_name (const char *name)
{
        char *normalized_name;
        char *language_code;
        char *territory_code;
        char *codeset;
        char *modifier;

        if (name[0] == '\0') {
                return NULL;
        }

        gdm_parse_language_name (name,
                                 &language_code,
                                 &territory_code,
                                 &codeset, &modifier);

        if (codeset != NULL) {
                make_codeset_canonical_for_locale (name, &codeset);
        }

        normalized_name = construct_language_name (language_code,
                                                   territory_code,
                                                   codeset, modifier);
        g_free (language_code);
        g_free (territory_code);
        g_free (codeset);
        g_free (modifier);

        return normalized_name;
}

static gboolean
language_name_is_valid (const char *language_name)
{
        char     *old_locale;
        gboolean  is_valid;

        old_locale = g_strdup (setlocale (LC_MESSAGES, NULL));
        is_valid = setlocale (LC_MESSAGES, language_name) != NULL;
        setlocale (LC_MESSAGES, old_locale);
        g_free (old_locale);

        return is_valid;
}

static gboolean
language_name_is_utf8 (const char *language_name)
{
        char     *old_locale;
        char     *codeset;
        gboolean  is_utf8;

        old_locale = g_strdup (setlocale (LC_CTYPE, NULL));

        if (setlocale (LC_CTYPE, language_name) == NULL) {
                g_free (old_locale);
                return FALSE;
        }

        codeset = normalize_codeset (nl_langinfo (CODESET));

        is_utf8 = strcmp (codeset, "utf8") == 0;
        g_free (codeset);

        setlocale (LC_CTYPE, old_locale);
        g_free (old_locale);

        return is_utf8;
}

static gboolean
language_name_has_translations (const char *language_name)
{
        GDir        *dir;
        char        *path;
        const char  *name;
        gboolean     has_translations;

        path = g_build_filename (GNOMELOCALEDIR, language_name, "LC_MESSAGES", NULL);

        has_translations = FALSE;
        dir = g_dir_open (path, 0, NULL);
        g_free (path);

        if (dir == NULL) {
                goto out;
        }

        do {
                name = g_dir_read_name (dir);

                if (name == NULL) {
                        break;
                }

                if (g_str_has_suffix (name, ".mo")) {
                        has_translations = TRUE;
                        break;
                }
        } while (name != NULL);
        g_dir_close (dir);

out:
        return has_translations;
}

static gboolean
add_locale (const char *language_name)
{
        GdmLocale *locale;
        GdmLocale *old_locale;
        char      *name;

        if (language_name_is_utf8 (language_name)) {
                name = g_strdup (language_name);
        } else {
                name = g_strdup_printf ("%s.utf8", language_name);

                if (!language_name_is_utf8 (name)) {
                        g_free (name);
                        return FALSE;
                }
        }

        if (!language_name_is_valid (name)) {
                g_free (name);
                return FALSE;
        }


        locale = g_new0 (GdmLocale, 1);
        gdm_parse_language_name (name,
                                 &locale->language_code,
                                 &locale->territory_code,
                                 &locale->codeset,
                                 &locale->modifier);
        g_free (name);
        name = NULL;

        locale->id = construct_language_name (locale->language_code, locale->territory_code,
                                              NULL, locale->modifier);
        locale->name = construct_language_name (locale->language_code, locale->territory_code,
                                                locale->codeset, locale->modifier);

        if (!language_name_has_translations (locale->name) &&
            !language_name_has_translations (locale->id) &&
            !language_name_has_translations (locale->language_code)) {
                gdm_locale_free (locale);
                return FALSE;
        }

        old_locale = g_hash_table_lookup (gdm_available_locales_map, locale->id);
        if (old_locale != NULL) {
                if (strlen (old_locale->name) > strlen (locale->name)) {
                        gdm_locale_free (locale);
                        return FALSE;
                }
        }

        g_hash_table_insert (gdm_available_locales_map, g_strdup (locale->id), locale);

        return TRUE;
}

struct nameent
{
        char    *name;
        uint32_t locrec_offset;
};

static gboolean
collect_locales_from_archive (void)
{
        GMappedFile        *mapped;
        GError             *error;
        char               *addr;
        struct locarhead   *head;
        struct namehashent *namehashtab;
        struct nameent     *names;
        uint32_t            used;
        uint32_t            cnt;
        gsize               len;
        gboolean            locales_collected;

        error = NULL;
        mapped = g_mapped_file_new (ARCHIVE_FILE, FALSE, &error);
        if (mapped == NULL) {
                g_warning ("Mapping failed for %s: %s", ARCHIVE_FILE, error->message);
                g_error_free (error);
                return FALSE;
        }

        locales_collected = FALSE;

        addr = g_mapped_file_get_contents (mapped);
        len = g_mapped_file_get_length (mapped);

        head = (struct locarhead *) addr;
        if (head->namehash_offset + head->namehash_size > len
            || head->string_offset + head->string_size > len
            || head->locrectab_offset + head->locrectab_size > len
            || head->sumhash_offset + head->sumhash_size > len) {
                goto out;
        }

        namehashtab = (struct namehashent *) (addr + head->namehash_offset);

        names = (struct nameent *) g_new0 (struct nameent, head->namehash_used);
        for (cnt = used = 0; cnt < head->namehash_size; ++cnt) {
                if (namehashtab[cnt].locrec_offset != 0) {
                        names[used].name = addr + namehashtab[cnt].name_offset;
                        names[used++].locrec_offset = namehashtab[cnt].locrec_offset;
                }
        }

        for (cnt = 0; cnt < used; ++cnt) {
                add_locale (names[cnt].name);
        }

        g_free (names);

        locales_collected = TRUE;
 out:
#if GLIB_CHECK_VERSION(2,22,0)
        g_mapped_file_unref(mapped);
#else
        g_mapped_file_free (mapped);
#endif
        return locales_collected;
}

static int
select_dirs (const struct dirent *dirent)
{
        int result = 0;

        if (strcmp (dirent->d_name, ".") != 0 && strcmp (dirent->d_name, "..") != 0) {
                mode_t mode = 0;

#ifdef _DIRENT_HAVE_D_TYPE
                if (dirent->d_type != DT_UNKNOWN && dirent->d_type != DT_LNK) {
                        mode = DTTOIF (dirent->d_type);
                } else
#endif
                        {
                                struct stat st;
                                char       *path;

                                path = g_build_filename (LIBLOCALEDIR, dirent->d_name, NULL);
                                if (g_stat (path, &st) == 0) {
                                        mode = st.st_mode;
                                }
                                g_free (path);
                        }

                result = S_ISDIR (mode);
        }

        return result;
}

static void
collect_locales_from_directory (void)
{
        struct dirent **dirents;
        int             ndirents;
        int             cnt;

        ndirents = scandir (ISO_CODES_LOCALESDIR, &dirents, select_dirs, alphasort);

        for (cnt = 0; cnt < ndirents; ++cnt) {
                add_locale (dirents[cnt]->d_name);
        }

        if (ndirents > 0) {
                free (dirents);
        }
}

static void
collect_locales (void)
{

        if (gdm_available_locales_map == NULL) {
                gdm_available_locales_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) gdm_locale_free);
        }

        if (collect_locales_from_archive ()) {
                return;
        } else {
                g_warning ("Could not read list of available locales from libc, "
                           "guessing possible locales from available translations, "
                           "but list may be incomplete!");

                collect_locales_from_directory ();
        }

}

static gboolean
is_fallback_language (const char *code)
{
        const char *fallback_language_names[] = { "C", "POSIX", NULL };
        int i;

        for (i = 0; fallback_language_names[i] != NULL; i++) {
                if (strcmp (code, fallback_language_names[i]) == 0) {
                        return TRUE;
                }
        }

        return FALSE;
}

static const char *
get_language (const char *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        if (is_fallback_language (code)) {
                return "Unspecified";
        }

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (gdm_languages_map, code);

        return name;
}

static char *
get_first_item_in_semicolon_list (const char *list)
{
    char **items;
    char  *item;

    /* Some entries in iso codes have multiple values, separated
     * by semicolons.  Not really sure which one to pick, so
     * we just arbitrarily pick the first one.
     */
    items = g_strsplit (list, "; ", 2);

    item = g_strdup (items[0]);
    g_strfreev (items);

    return item;
}

static char *
get_translated_language (const char *code,
                         const char *locale)
{
        const char *language;
        char *name;

        language = get_language (code);

        name = NULL;
        if (language != NULL) {
                const char  *translated_name;
                char        *old_locale;

                if (locale != NULL) {
                        old_locale = g_strdup (setlocale (LC_MESSAGES, NULL));
                        setlocale (LC_MESSAGES, locale);
                        setlocale (LC_ALL, locale);
                }

                if (is_fallback_language (code)) {
                        name = _("Unspecified");
                } else {
                        translated_name = dgettext ("iso_639", language);
                        name = get_first_item_in_semicolon_list (translated_name);
                }

                if (locale != NULL) {
                        setlocale (LC_MESSAGES, old_locale);
                        g_free (old_locale);
                }
        }

        return name;
}

static const char *
get_territory (const char *code)
{
        const char *name;
        int         len;

        g_assert (code != NULL);

        len = strlen (code);
        if (len != 2 && len != 3) {
                return NULL;
        }

        name = (const char *) g_hash_table_lookup (gdm_territories_map, code);

        return name;
}

static const char *
get_translated_territory (const char *code,
                          const char *locale)
{
        const char *territory;
        char *name;

        territory = get_territory (code);

        name = NULL;
        if (territory != NULL) {
                const char *translated_territory;
                char       *old_locale;

                if (locale != NULL) {
                        old_locale = g_strdup (setlocale (LC_MESSAGES, NULL));
                        setlocale (LC_MESSAGES, locale);
                }

                translated_territory = dgettext ("iso_3166", territory);
                name = get_first_item_in_semicolon_list (translated_territory);

                if (locale != NULL) {
                        setlocale (LC_MESSAGES, old_locale);
                        g_free (old_locale);
                }
        }

        return name;
}

static void
languages_parse_start_tag (GMarkupParseContext      *ctx,
                           const char               *element_name,
                           const char              **attr_names,
                           const char              **attr_values,
                           gpointer                  user_data,
                           GError                  **error)
{
        const char *ccode_longB;
        const char *ccode_longT;
        const char *ccode;
        const char *lang_name;

        if (! g_str_equal (element_name, "iso_639_entry") || attr_names == NULL || attr_values == NULL) {
                return;
        }

        ccode = NULL;
        ccode_longB = NULL;
        ccode_longT = NULL;
        lang_name = NULL;

        while (*attr_names && *attr_values) {
                if (g_str_equal (*attr_names, "iso_639_1_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 2) {
                                        return;
                                }
                                ccode = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "iso_639_2B_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ccode_longB = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "iso_639_2T_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ccode_longT = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "name")) {
                        lang_name = *attr_values;
                }

                ++attr_names;
                ++attr_values;
        }

        if (lang_name == NULL) {
                return;
        }

        if (ccode != NULL) {
                g_hash_table_insert (gdm_languages_map,
                                     g_strdup (ccode),
                                     g_strdup (lang_name));
        }
        if (ccode_longB != NULL) {
                g_hash_table_insert (gdm_languages_map,
                                     g_strdup (ccode_longB),
                                     g_strdup (lang_name));
        }
        if (ccode_longT != NULL) {
                g_hash_table_insert (gdm_languages_map,
                                     g_strdup (ccode_longT),
                                     g_strdup (lang_name));
        }
}

static void
territories_parse_start_tag (GMarkupParseContext      *ctx,
                             const char               *element_name,
                             const char              **attr_names,
                             const char              **attr_values,
                             gpointer                  user_data,
                             GError                  **error)
{
        const char *acode_2;
        const char *acode_3;
        const char *ncode;
        const char *territory_common_name;
        const char *territory_name;

        if (! g_str_equal (element_name, "iso_3166_entry") || attr_names == NULL || attr_values == NULL) {
                return;
        }

        acode_2 = NULL;
        acode_3 = NULL;
        ncode = NULL;
        territory_common_name = NULL;
        territory_name = NULL;

        while (*attr_names && *attr_values) {
                if (g_str_equal (*attr_names, "alpha_2_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 2) {
                                        return;
                                }
                                acode_2 = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "alpha_3_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                acode_3 = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "numeric_code")) {
                        /* skip if empty */
                        if (**attr_values) {
                                if (strlen (*attr_values) != 3) {
                                        return;
                                }
                                ncode = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "common_name")) {
                        /* skip if empty */
                        if (**attr_values) {
                                territory_common_name = *attr_values;
                        }
                } else if (g_str_equal (*attr_names, "name")) {
                        territory_name = *attr_values;
                }

                ++attr_names;
                ++attr_values;
        }

        if (territory_common_name != NULL) {
                territory_name = territory_common_name;
        }

        if (territory_name == NULL) {
                return;
        }

        if (acode_2 != NULL) {
                g_hash_table_insert (gdm_territories_map,
                                     g_strdup (acode_2),
                                     g_strdup (territory_name));
        }
        if (acode_3 != NULL) {
                g_hash_table_insert (gdm_territories_map,
                                     g_strdup (acode_3),
                                     g_strdup (territory_name));
        }
        if (ncode != NULL) {
                g_hash_table_insert (gdm_territories_map,
                                     g_strdup (ncode),
                                     g_strdup (territory_name));
        }
}

static void
languages_init (void)
{
        GError  *error;
        gboolean res;
        char    *buf;
        gsize    buf_len;

        bindtextdomain ("iso_639", ISO_CODES_LOCALESDIR);
        bind_textdomain_codeset ("iso_639", "UTF-8");

        gdm_languages_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        error = NULL;
        res = g_file_get_contents (ISO_CODES_DATADIR "/iso_639.xml",
                                   &buf,
                                   &buf_len,
                                   &error);
        if (res) {
                GMarkupParseContext *ctx;
                GMarkupParser        parser = { languages_parse_start_tag, NULL, NULL, NULL, NULL };

                ctx = g_markup_parse_context_new (&parser, 0, NULL, NULL);

                error = NULL;
                res = g_markup_parse_context_parse (ctx, buf, buf_len, &error);

                if (! res) {
                        g_warning ("Failed to parse '%s': %s\n",
                                   ISO_CODES_DATADIR "/iso_639.xml",
                                   error->message);
                        g_error_free (error);
                }

                g_markup_parse_context_free (ctx);
                g_free (buf);
        } else {
                g_warning ("Failed to load '%s': %s\n",
                           ISO_CODES_DATADIR "/iso_639.xml",
                           error->message);
                g_error_free (error);
        }
}

static void
territories_init (void)
{
        GError  *error;
        gboolean res;
        char    *buf;
        gsize    buf_len;

        bindtextdomain ("iso_3166", ISO_CODES_LOCALESDIR);
        bind_textdomain_codeset ("iso_3166", "UTF-8");

        gdm_territories_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        error = NULL;
        res = g_file_get_contents (ISO_CODES_DATADIR "/iso_3166.xml",
                                   &buf,
                                   &buf_len,
                                   &error);
        if (res) {
                GMarkupParseContext *ctx;
                GMarkupParser        parser = { territories_parse_start_tag, NULL, NULL, NULL, NULL };

                ctx = g_markup_parse_context_new (&parser, 0, NULL, NULL);

                error = NULL;
                res = g_markup_parse_context_parse (ctx, buf, buf_len, &error);

                if (! res) {
                        g_warning ("Failed to parse '%s': %s\n",
                                   ISO_CODES_DATADIR "/iso_3166.xml",
                                   error->message);
                        g_error_free (error);
                }

                g_markup_parse_context_free (ctx);
                g_free (buf);
        } else {
                g_warning ("Failed to load '%s': %s\n",
                           ISO_CODES_DATADIR "/iso_3166.xml",
                           error->message);
                g_error_free (error);
        }
}

char *
gdm_get_language_from_name (const char *name,
                            const char *locale)
{
        char *full_language;
        char *language_code;
        char *territory_code;
        const char *language;
        const char *territory;

        if (gdm_languages_map == NULL) {
                languages_init ();
        }

        if (gdm_territories_map == NULL) {
                territories_init ();
        }

        language_code = NULL;
        territory_code = NULL;
        full_language = NULL;

        gdm_parse_language_name (name, &language_code, &territory_code,
                                 NULL, NULL);

        if (language_code == NULL) {
                goto out;
        }

        language = get_translated_language (language_code, locale);

        if (territory_code != NULL) {
                territory = get_translated_territory (territory_code, locale);
        } else {
                territory = NULL;
        }

        if (territory != NULL) {
                full_language  = g_strdup_printf ("%s (%s)",
                        language ? language : "",
                        territory ? territory : "");
        } else {
                full_language  = g_strdup (language);
        }

out:
       g_free (language_code);
       g_free (territory_code);

       return full_language;
}

char **
gdm_get_all_language_names (void)
{
        GHashTableIter iter;
        gpointer key, value;
        GPtrArray *array;

        if (gdm_available_locales_map == NULL) {
                collect_locales ();
        }

        array = g_ptr_array_new ();
        g_hash_table_iter_init (&iter, gdm_available_locales_map);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmLocale *locale;

                locale = (GdmLocale *) value;

                g_ptr_array_add (array, g_strdup (locale->name));
        }
        g_ptr_array_add (array, NULL);

        return (char **) g_ptr_array_free (array, FALSE);
}
