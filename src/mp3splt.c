/*
 * Mp3Splt -- Utility for mp3/ogg splitting without decoding
 *
 * Copyright (c) 2002-2005 M. Trotta - <mtrotta@users.sourceforge.net>
 * Copyright (c) 2005-2010 Alexandru Munteanu - <io_fx@yahoo.fr>
 *
 * http://mp3splt.sourceforge.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <getopt.h>
#include <locale.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#ifdef __WIN32__
#include <windows.h>
#include <shlwapi.h>
#endif

#include <libmp3splt/mp3splt.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define VERSION "2.2.9"
#define PACKAGE_NAME "mp3splt"
#endif

#define MP3SPLT_DATE "27/09/10"
#define MP3SPLT_AUTHOR1 "Matteo Trotta"
#define MP3SPLT_AUTHOR2 "Alexandru Munteanu"
#define MP3SPLT_EMAIL1 "<mtrotta AT users.sourceforge.net>"
#define MP3SPLT_EMAIL2 "<io_fx AT yahoo.fr>"
#define MP3SPLT_CDDBFILE "query.cddb"

#ifdef ENABLE_NLS
#  define MP3SPLT_GETTEXT_DOMAIN "mp3splt"
#  define _(STR) gettext(STR)
#else
#  define _(STR) ((const char *)STR)
#endif

//in case of STDIN/STDOUT usage, we change the console file handle
//-yeah indeed, global variables might suck
FILE *console_out = NULL;
FILE *console_err = NULL;
FILE *console_progress = NULL;

typedef struct {
  //force id3v1 tags, force id3v2 tags or both
  short T_option;
  short T_option_value;
  //wrap split, list wrap options, error split
  short w_option; short l_option; short e_option;
  //frame mode, cddb/cue option, time split
  short f_option; short c_option; short t_option;
  //silence split, adjust option, parameters
  short s_option; short a_option; short p_option;
  //output filename, output directory, seekable
  short o_option; short d_option; short k_option;
  //custom tags, no tags, quiet option
  short g_option; short n_option; short q_option;
  short E_option;
  short P_option;
  short x_option;
  short N_option;
  short O_option;
  short X_option;
  short A_option;
  //-Q option
  short qq_option;
  //info -i option, m3u file option
  short i_option;
  short m_option;
  short S_option;
  int S_option_value;
  //cddb argument, output dir argument, parameters arguments with -p
  char *cddb_arg; char *dir_arg; char *param_args;
  char *export_cue_arg;
  char *audacity_labels_arg;
  //the m3u filename
  char *m3u_arg;
  //custom tags with -g
  char *custom_tags;
  //output format (-o)
  char *output_format;
  //the parsed freedb_search_type
  //the parsed freedb_search_server
  //the parsed freedb_search_port
  int freedb_search_type;
  char freedb_search_server[256];
  int freedb_search_port;
  //the parsed freedb_get_type
  //the parsed freedb_get_server
  //the parsed freedb_get_port
  int freedb_get_type;
  char freedb_get_server[256];
  int freedb_get_port;
  //the search string passed in parameter -c query{my artist}
  char freedb_arg_search_string[2048];
  //the chosen result passed in parameter: -c query{my artist}[
  int freedb_arg_result_option;
} options;

typedef struct
{
  double level_sum;
  unsigned long number_of_levels;
  //if set to FALSE, don't show the average silence level
  int print_silence_level;
} silence_level;

typedef struct
{
  //command line options
  options *opt;
  //the libmp3splt state
  splt_state *state;
  //for computing the average silence level
  silence_level *sl;
  //the filenames parsed from the arguments
  char **filenames;
  int number_of_filenames;
  //the splitpoints parsed from the arguments
  long *splitpoints;
  int number_of_splitpoints;
  //command line arguments: on windows, we need to
  //keep the ones transformed to utf8 and free them later
  char **argv;
  int argc;
} main_data;

//we make a global variable, we use it in
//sigint_handler
splt_state *state;

//free the option struct
void free_options(options **opt)
{
  if (opt)
  {
    if (*opt)
    {
      if ((*opt)->dir_arg)
      {
        free((*opt)->dir_arg);
        (*opt)->dir_arg = NULL;
      }

      if ((*opt)->cddb_arg)
      {
        free((*opt)->cddb_arg);
        (*opt)->cddb_arg = NULL;
      }

      if ((*opt)->export_cue_arg)
      {
        free((*opt)->export_cue_arg);
        (*opt)->export_cue_arg = NULL;
      }

      if ((*opt)->audacity_labels_arg)
      {
        free((*opt)->audacity_labels_arg);
        (*opt)->audacity_labels_arg = NULL;
      }

      if ((*opt)->m3u_arg)
      {
        free((*opt)->m3u_arg);
        (*opt)->m3u_arg = NULL;
      }

      if ((*opt)->param_args)
      {
        free((*opt)->param_args);
        (*opt)->param_args = NULL;
      }

      if ((*opt)->custom_tags)
      {
        free((*opt)->custom_tags);
        (*opt)->custom_tags = NULL;
      }

      if ((*opt)->output_format)
      {
        free((*opt)->output_format);
        (*opt)->output_format = NULL;
      }
      free(*opt);
      *opt = NULL;
    }
  }
}

void free_main_struct(main_data **d)
{
  if (d)
  {
    main_data *data = *d;
    if (data)
    {
      //try to stop the split
      mp3splt_stop_split(data->state, NULL);
      //free options
      free_options(&data->opt);

      //free silence level
      if (data->sl)
      {
        free(data->sl);
        data->sl = NULL;
      }

      //free filenames & splitpoints
      if (data->filenames)
      {
        int i = 0;
        for (i = 0; i < data->number_of_filenames;i++)
        {
          free(data->filenames[i]);
          data->filenames[i] = NULL;
        }
        free(data->filenames);
        data->filenames = NULL;
      }
      if (data->splitpoints)
      {
        free(data->splitpoints);
        data->splitpoints = NULL;
      }

#ifdef __WIN32__
      //free argv
      if (data->argv)
      {
        int i = 0;
        for (i = 0; i < data->argc;i++)
        {
          if (data->argv[i])
          {
            free(data->argv[i]);
            data->argv[i] = NULL;
          }
        }

        free(data->argv);
        data->argv = NULL;
      }
#endif 

      //free left variables in the state
      mp3splt_free_state(data->state, NULL);
      data->state = NULL;

      free(data);
      data = NULL;
    }
  }
}

//prints a message
void print_message(const char *m)
{
  fprintf(console_out,"%s\n",m);
  fflush(console_out);
}

//prints a warning
void print_warning(const char *w)
{
  fprintf(console_err,_(" Warning: %s\n"),w);
  fflush(console_err);
}

//prints an error
void print_error(const char *e)
{
  fprintf(console_err,_(" Error: %s\n"),e);
  fflush(console_err);
}

void print_error_exit(const char *m, main_data *data)
{
  print_error(m);
  free_main_struct(&data);
  exit(1);
}

void print_message_exit(const char *m, main_data *data)
{
  print_message(m);
  free_main_struct(&data);
  exit(0);
}

void *my_malloc(size_t size, main_data *data)
{
  void *allocated = malloc(size);
  if (! allocated)
  {
    print_error_exit(_("cannot allocate memory !"), data);
  }
  else
  {
    return allocated;
  }

  return NULL;
}

void *my_realloc(void *ptr, size_t size, main_data *data)
{
  void *allocated = realloc(ptr, size);
  if (! allocated)
  {
    print_error_exit(_("cannot allocate memory !"), data);
  }
  else
  {
    return allocated;
  }
  
  return NULL;
}

//shows a small mp3splt help and exits with error 1
void show_small_help_exit(main_data *data)
{
  free_main_struct(&data);

  print_message(_("\n"
        "USAGE:\n"
        "      mp3splt [OPTIONS] FILE1 [FILE2] ... [BEGIN_TIME] [TIME] ... [END_TIME]\n"
        "      TIME FORMAT: min.sec[.0-99], even if minutes are over 59\n"
        "                   (or EOF for End Of File). "));
  print_message(_("\nOPTIONS (split mode options)\n"
        " -t + TIME: to split files every fixed time len. (TIME format same as above). \n"
        " -c + file.cddb, file.cue or \"query\" or \"query{album}\". Get splitpoints and\n"
        "      filenames from a .cddb or .cue file or from Internet (\"query\").\n"
        "      Use -a to auto-adjust splitpoints."));
  print_message(_(" -s   Silence detection: automatically find splitpoint. (Use -p for arguments)\n"
        " -w   Splits wrapped files created with Mp3Wrap or AlbumWrap.\n"
        " -l   Lists the tracks from file without extraction. (Only for wrapped mp3)\n"
        " -e   Error mode: split mp3 with sync error detection. (For concatenated mp3)"));
  print_message(_(" -A + AUDACITY_FILE: split with splitpoints from the audacity labels file"));
  print_message(_(" -S + SPLIT_NUMBER: split in SPLIT_NUMBER equal time files"));
  print_message(_(" -i   Count how many silence splitpoints we have with silence detection\n"
        "      (Use -p for arguments)\n"
        " -v   Prints current version and exits\n"
        " -h   Shows this help"));
  print_message(_("\n(other options)\n"
        " -T + TAGS_VERSION: for mp3 files, force output tags as version 1, 2 or 1 & 2.\n"
        "      TAGS_VERSION can be 1, 2 or 12\n"
        "      (default is to set the same version as the file to split)"));
  print_message(_(" -m + M3U_FILE: Appends to the specified m3u file the split filenames.\n"
        " -f   Frame mode (mp3 only): process all frames. For higher precision and VBR.\n"
        " -a   Auto-Adjust splitpoints with silence detection. (Use -p for arguments)"));
  print_message(_(" -p + PARAMETERS (th, nt, off, min, rm, gap): user arguments for -s and -a.\n"
        " -o + FORMAT: output filename pattern. Can contain those variables:\n"
        "      @a: artist tag, @p: performer tag (might not exists), @b: album tag\n"
        "      @t: title tag, @n: track number identifier, @N: track tag number\n"
        "      (a digit may follow the 'n' or 'N' for the number of digits to output),\n"
        "      @f: original filename"));
  print_message(_(" -g + TAGS: custom tags for the split files.\n"
        "      TAGS can contain those variables: \n"
        "         @a, @b, @t, @y, @c, @n, @o (set original tags),\n"
        "         @N (auto increment track number).\n"
        "      TAGS format is like [@a=artist1,@t=title1]%[@o,@N=2,@a=artist2]\n"
        "       (% means that we set the tags for all remaining files)"));
  print_message(_(" -d + DIRNAME: to put all output files in the directory DIRNAME.\n"
        " -k   Consider input not seekable (slower). Default when input is STDIN (-).\n"
        " -O + TIME: Overlap split files with TIME (slower)."));
  print_message(_(" -n   No Tag: does not write ID3v1 or vorbis comment. If you need clean files.\n"
        " -x   No Xing header: does not write the Xing header. Use with -n if you wish\n"
        "      to concatenate the split files\n"
        " -N   Don't create the 'mp3splt.log' log file when using '-s'."));
  print_message(_(" -P   Pretend to split: simulation of the process, without creating any\n"
                  "      files or directories"));
  print_message(_(" -E + CUE_FILE: export splitpoints to CUE file (use with -P if needed)"));
  print_message(_(" -q   Quiet mode: try not to prompt (if possible) and print less messages.\n"
        " -Q   Very quiet mode: don't print anything to stdout and no progress bar\n"
        "       (also enables -q).\n"
        " -D   Debug mode: used to debug the program.\n\n"
        "      Please read man page for complete documentation.\n"));

  if (console_out == stderr)
  {
    exit(1);
  }
  else
  {
    exit(0);
  }
}

//
char **rmopt(char **argv, int offset, int tot)
{
#ifdef __WIN32__
  int i = 0;
  for (i = 0; i < offset;i++)
  {
    if (argv[i])
    {
      free(argv[i]);
      argv[i] = NULL;
    }
  }
#endif

  char **first = &argv[1];
  while (offset < tot)
  {
    *first = argv[offset];
    first++;
    offset++;
  }

  return argv;
}

//parse the -p option
int parse_arg(char *arg, float *th, int *gap,
    int *nt, float *off, int *rm, float *min)
{
  char *ptr = NULL;
  int found = 0;

  if ((gap!=NULL) && ((ptr=strstr(arg, "gap"))!=NULL))
  {
    if ((ptr=strchr(ptr, '='))!=NULL)
    {
      if (sscanf(ptr+1, "%d", gap)==1)
      {
        found++;
      }
      else 
      {
        print_warning(_("bad gap argument. It will be ignored !"));
      }
    }
  }

  if ((th!=NULL) && ((ptr=strstr(arg, "th"))!=NULL))
  {
    if ((ptr=strchr(ptr, '='))!=NULL)
    {
      if (sscanf(ptr+1, "%f", th)==1)
      {
        found++;
      }
      else 
      {
        print_warning(_("bad threshold argument. It will be ignored !"));
      }
    }
  }

  if ((nt!=NULL) && ((ptr=strstr(arg, "nt"))!=NULL))
  {
    if ((ptr=strchr(ptr, '='))!=NULL)
    {
      if (sscanf(ptr+1, "%d", nt)==1)
      {
        found++;
      }
      else 
      {
        print_warning(_("bad tracknumber argument. It will be ignored !"));
      }
    }
  }

  if (rm!=NULL)
  {
    if ((ptr=strstr(arg, "rm"))!=NULL)
    {
      found++;
      *rm = 1;
    }
  }

  if ((off!=NULL) && ((ptr=strstr(arg, "off"))!=NULL))
  {
    if ((ptr=strchr(ptr, '='))!=NULL)
    {
      if (sscanf(ptr+1, "%f", off)==1)
      {
        found++;
      }
      else 
      {
        print_warning(_("bad offset argument. It will be ignored!"));
      }
    }
  }

  if ((min!=NULL) && ((ptr=strstr(arg, "min"))!=NULL))
  {
    if ((ptr=strchr(ptr, '='))!=NULL)
    {
      if (sscanf(ptr+1, "%f", min)==1)
      {
        found++;
      }
      else 
      {
        print_warning(_("bad minimum silence length argument. It will be ignored !"));
      }
    }
  }

  return found;
}

int we_have_incompatible_stdin_option(options *opt)
{
  return opt->s_option || opt->w_option ||
    opt->l_option || opt->e_option ||
    opt->i_option || opt->a_option ||
    opt->p_option || opt->S_option;
}

//check if we have the correct arguments
void check_args(int argc, main_data *data)
{
  options *opt = data->opt;

  if (argc < 2)
  {
    console_out = stderr;
    show_small_help_exit(data);
  }
  else
  {
    //if we want input not seekable (-k)
    if (opt->k_option)
    {
      if (we_have_incompatible_stdin_option(opt))
      {
        print_error_exit(_("cannot use -k option (or STDIN) with"
              " one of the following options: -S -s -w -l -e -i -a -p"), data);
      }
    }

    //if we dewrap (-w)
    if (opt->w_option)
    {
      if (opt->t_option || opt->c_option ||
          opt->s_option || opt->l_option ||
          opt->e_option || opt->i_option ||
          opt->f_option || opt->a_option ||
          opt->p_option || opt->o_option ||
          opt->g_option || opt->n_option ||
          opt->x_option || opt->A_option ||
          opt->E_option || opt->S_option)
      {
        print_error_exit(_("the -w option can only be used with -m, -d, -q and -Q"), data);
      }
    }

    //if we list wrapped files (-l)
    if (opt->l_option)
    {
      if (opt->t_option || opt->c_option ||
          opt->s_option || opt->e_option ||
          opt->i_option || opt->m_option ||
          opt->f_option || opt->a_option ||
          opt->p_option || opt->o_option ||
          opt->g_option || opt->d_option ||
          opt->n_option || opt->qq_option ||
          opt->x_option || opt->A_option ||
          opt->S_option)
      {
        print_error_exit(_("the -l option can only be used with -q"), data);
      }
    }

    //error mode (-e)
    if (opt->e_option)
    {
      if (opt->t_option || opt->c_option || 
          opt->s_option || opt->i_option || 
          opt->a_option || opt->p_option ||
          opt->g_option || opt->n_option ||
          opt->A_option || opt->E_option ||
          opt->S_option)
      {
        print_error_exit(_("the -e option can only be used with -m, -f, -o, -d, -q, -Q"), data);
      }
    }

    //frame mode (-f)
    if (opt->f_option)
    {
    }

    //cddb/cue/freedb (-c)
    if (opt->c_option)
    {
      if (opt->t_option || opt->s_option ||
          opt->i_option || opt->g_option ||
          opt->A_option || opt->S_option)
      {
        print_error_exit(_("the -c option cannot be used with -t, -g, -s, -A, -i or -S"), data);
      }
    }

    if (opt->A_option)
    {
      if (opt->t_option || opt->s_option || opt->i_option || opt->S_option)
      {
        print_error_exit(_("the -A option cannot be used with -t, -s, -i or -S"), data);
      }
    }

    //time split (-t)
    if (opt->t_option)
    {
      if (opt->s_option || opt->i_option || opt->S_option)
      {
        print_error_exit(_("the -t option cannot be used with -s, -i or -S"), data);
      }
    }

    //silence split (-s)
    if (opt->s_option)
    {
      if (opt->a_option || opt->i_option || opt->S_option)
      {
        print_error_exit(_("-s option cannot be used with -a, -i or -S"), data);
      }
    }

    //auto adjust option (-a)
    if (opt->a_option)
    {
      if (opt->i_option)
      {
        print_error_exit(_("-a option cannot be used with -i"), data);
      }
    }

    if (opt->S_option)
    {
    }

    //parameters (-p)
    if (opt->p_option)
    {
      if (!opt->a_option && !opt->s_option && !opt->i_option)
      {
        print_error_exit(_("the -p option cannot be used without -a, -s or -i"), data);
      }
    }

    //output format (-o)
    if (opt->o_option)
    {
      if (opt->i_option)
      {
        print_error_exit(_("the -o option cannot be used with -i"), data);
      }
      if (opt->output_format)
      {
        if ((strcmp(opt->output_format,"-") == 0) && (opt->m_option || opt->d_option))
        {
          print_error_exit(_("cannot use '-o -' (STDOUT) with -m or -d"), data);
        }
      }
    }

    //custom tags (-g)
    if (opt->g_option)
    {
      if (opt->i_option || opt->n_option)
      {
        print_error_exit(_("the -g option cannot be used with -n or -i"), data);
      }
    }

    //directory (-d)
    if (opt->d_option)
    {
      if (opt->i_option)
      {
        print_error_exit(_("the -d option cannot be used with -i"), data);
      }
    }

    //no tags (-n)
    if (opt->n_option)
    {
      if (opt->i_option || opt->T_option)
      {
        print_error_exit(_("the -n option cannot be used with -i or -T"), data);
      }
    }

    //generate m3u file (-m)
    if (opt->m_option)
    {
      if (opt->i_option)
      {
        print_error_exit(_("the -m option cannot be used with -i"), data);
      }
    }

    //count silence splitpoints (-i)
    if (opt->i_option)
    {
    }

    //quiet mode (-q)
    if (opt->q_option)
    {
    }

    //very quiet mode (-Q)
    if (opt->qq_option)
    {
      if (opt->o_option)
      {
        if (strcmp(opt->output_format,"-") == 0)
        {
          print_error_exit(_("the -Q option cannot be used with"
              " STDOUT output ('-o -')"), data);
        }
      }
      if (opt->c_option)
      {
        if (strncmp(opt->cddb_arg,"query",5) == 0)
        {
          print_error_exit(_("the -Q option cannot be used with"
              " interactive freedb query ('-c query')"), data);
        }
      }
    }

    if (opt->N_option)
    {
      if (!opt->s_option)
      {
        print_error_exit(_("the -N option must be used with"
            " silence detection (-s option)"), data);
      }
    }

    if (opt->O_option)
    {
      if (opt->w_option || opt->e_option ||
          opt->l_option || opt->i_option)
      {
        print_error_exit(_("the -O option cannot be used with"
            " -w, -e, -l or -i"), data);
      }
    }

    if (opt->x_option)
    {
    }

    if (opt->T_option)
    {
      int force_tags_version = opt->T_option_value;
      if ((force_tags_version != 1) && (force_tags_version != 2) &&
          (force_tags_version != 12))
      {
        print_error_exit("the -T option can only have values 1, 2 or 12", data);
      }
    }
  }
}

//prints a confirmation error that comes from the library
void process_confirmation_error(int conf, main_data *data)
{
  char *error_from_library = NULL;
  error_from_library = mp3splt_get_strerror(data->state, conf);
  if (error_from_library != NULL)
  {
    if (conf >= 0)
    {
      print_message(error_from_library);
      free(error_from_library);
    }
    else
    {
      fprintf(console_err,"%s\n",error_from_library);
      fflush(console_err);
      free(error_from_library);
      free_main_struct(&data);
      exit(1);
    }
    error_from_library = NULL;
  }
  if (conf == SPLT_DEWRAP_OK)
  {
    print_message(_("\nAll files have been split correctly."
          " Visit http://mp3wrap.sourceforge.net!"));
  }
}

//returns the converted string s in hundredth of seconds
//returns -1 if it cannot convert
long c_hundreths(const char *s)
{
  long minutes=0, seconds=0, hundredths=0, i;
  long hun = -1;

  if (strcmp(s,"EOF") == 0)
  {
    return LONG_MAX;
  }

  for(i=0; i<strlen(s); i++) // Some checking
  {
    if ((s[i]<0x30 || s[i] > 0x39) && (s[i]!='.'))
    {
      return -1;
    }
  }

  if (sscanf(s, "%ld.%ld.%ld", &minutes, &seconds, &hundredths) < 2)
  {
    return -1;
  }

  if ((minutes < 0) || (seconds < 0) || (hundredths < 0))
  {
    return -1;
  }

  if ((seconds > 59) || (hundredths > 99))
  {
    return -1;
  }

  if (s[strlen(s)-2] == '.')
  {
    hundredths *= 10;
  }

  hun = hundredths;
  hun += (minutes*60 + seconds) * 100;

  return hun;
}

//for the moment 2 ways of getting the file and one way to search it
//freedb get type can be: cddb_cgi or cddb_protocol
//freedb search type can be: cddb_cgi or web_search
//query[get=cddb_cgi://freedb2.org/~cddb/cddb.cgi:80,search=cddb_cgi://freedb2.org/~cddb/cddb.cgi:80]
//query[get=cddb_protocol://freedb.org:8880,search=cddb_cgi://freedb2.org/~cddb/cddb.cgi:80]
//query[get=cddb_cgi://freedb.org/~cddb/cddb.cgi:80,search=cddb_cgi://freedb2.org/~cddb/cddb.cgi:80]
//query[get...]{search_string}
//query[get...]{search_string}(chosen_result_int)
//we parse the query arguments
int parse_query_arg(options *opt, const char *query)
{
  const char *cur_pos = NULL;
  const char *end_pos = NULL;
  const char *test_pos = NULL;
  cur_pos = query + 5;

  short ambigous = SPLT_FALSE;

  //if we have [get=...]
  if (cur_pos[0] == '[')
  {
    cur_pos = strchr(query,'[');

    //if we don't have ], ambigous
    if (!(test_pos = strchr(cur_pos,']')))
    {
      ambigous = SPLT_TRUE;
    }

    int search_found = SPLT_FALSE;
    int get_found = SPLT_FALSE;
    int paranthesis_has_content = SPLT_FALSE;
    //we find "get" or "search"
    while((test_pos = strstr(cur_pos,"get="))||
        (test_pos = strstr(cur_pos,"search=")))
    {
      paranthesis_has_content = SPLT_TRUE;

      //we find out which one is first
      end_pos = strstr(cur_pos,"get=");
      test_pos = strstr(cur_pos,"search=");
      if (end_pos == NULL)
      {
        cur_pos = test_pos;
      }
      else
      {
        if (test_pos == NULL)
        {
          cur_pos = end_pos;
        }
        else
        {
          if (end_pos < test_pos)
          {
            cur_pos = end_pos;
          }
          else
          {
            cur_pos = test_pos;
          }
        }
      }

      //we determine the type (get or search)
      if (strstr(cur_pos,"get=") == cur_pos)
      {
        get_found = SPLT_TRUE;
        search_found = SPLT_FALSE;
        cur_pos += 4;
      }
      else
      {
        search_found = SPLT_TRUE;
        get_found = SPLT_FALSE;
        cur_pos += 7;
      }

      //we get out the type of the search
      char freedb_type[256] = "\0";
      if ((end_pos=strstr(cur_pos,"://"))||
          (end_pos=strchr(cur_pos,',')) ||
          (end_pos=strchr(cur_pos,']')))
      {
        if (end_pos-cur_pos < 255)
        {
          snprintf(freedb_type,end_pos-cur_pos+1,"%s",cur_pos);
          freedb_type[end_pos-cur_pos] = '\0';
          if ((strchr(cur_pos,',')==end_pos) ||
              (strchr(cur_pos,']')==end_pos))
          {
            cur_pos = end_pos;
          }
          else
          {
            cur_pos = end_pos+3;
          }
          end_pos = cur_pos;
        }
      }
      else
      {
        end_pos = cur_pos;
      }

      //we get out the server
      char freedb_server[256] = "\0";
      while ((*end_pos != ':') && (*end_pos != ',') &&
          (*end_pos != ']') && (*end_pos != '\0'))
      {
        end_pos++;
      }
      if ((end_pos != cur_pos) && (end_pos-cur_pos < 255)
          && (*end_pos != '\0'))
      {
        snprintf(freedb_server,end_pos-cur_pos+1,"%s",cur_pos);
        freedb_server[end_pos-cur_pos] = '\0';
        if (*(end_pos+1) == ']' || *(end_pos+1) == ',')
        {
          cur_pos = end_pos+1;
        }
        else
        {
          cur_pos = end_pos;
        }
        end_pos = cur_pos;
      }
      else
      {
        cur_pos = end_pos;
      }

      //we get out the port
      char freedb_port[10] = "\0";
      int is_only_digits = SPLT_TRUE;
      int freedb_int_port = -1;
      //if we have the port
      if (*end_pos == ':')
      {
        cur_pos++;
        end_pos++;
        while((*end_pos != ']') &&
            (*end_pos != '\0') &&
            (*end_pos != ','))
        {
          //we have to have only digits for the port
          if (!isdigit(*end_pos))
          {
            ambigous = SPLT_TRUE;
            is_only_digits = SPLT_FALSE;
          }
          end_pos++;
        }
        if ((end_pos != cur_pos) && (end_pos-cur_pos < 10))
        {
          snprintf(freedb_port,end_pos-cur_pos+1,"%s",cur_pos);
          freedb_port[end_pos-cur_pos] = '\0';
          cur_pos = end_pos;
        }

        //we get the port as integer
        if (*freedb_port != '\0')
        {
          if (is_only_digits)
          {
            freedb_int_port = atoi(freedb_port);
          }
          else
          {
            freedb_int_port = SPLT_FREEDB_CDDB_CGI_PORT;
            print_warning(_("found non digits characters in port !"
                  " (switched to default)"));
          }
        }
      }

      //we put the search type as integer
      int freedb_int_type = -1;
      if (*freedb_type != '\0')
      {
        //get type
        if (get_found)
        {
          if (strcmp(freedb_type,"cddb_protocol") == 0)
          {
            freedb_int_type = SPLT_FREEDB_GET_FILE_TYPE_CDDB;
          }
          else
          {
            if (strcmp(freedb_type,"cddb_cgi") == 0)
            {
              freedb_int_type = SPLT_FREEDB_GET_FILE_TYPE_CDDB_CGI;
            }
            else
            {
              print_warning(_("unknown search type !"
                    " (switched to default)"));
            }
          }
        }
        else
        {
          //search type
          if (strcmp(freedb_type,"cddb_cgi") == 0)
          {
            freedb_int_type = SPLT_FREEDB_SEARCH_TYPE_CDDB_CGI;
          }
          else
          {
            if (strcmp(freedb_type,"web_search") == 0)
            {
              print_warning(_("freedb web search not implemented yet !"
                    " (switched to default)"));
              freedb_int_type = SPLT_FREEDB_SEARCH_TYPE_CDDB_CGI;
            }
            else
            {
              print_warning(_("unknown get type ! (switched to default)"));
            }
          }
        }
      }
      else
      {
        ambigous = SPLT_TRUE;
      }

      //if we have found search
      if (search_found)
      {
        if (freedb_int_type != -1)
        {
          opt->freedb_search_type = freedb_int_type;
        }
        if (*freedb_server != '\0')
        {
          snprintf(opt->freedb_search_server,255, "%s",freedb_server);
        }
        else
        {
          snprintf(opt->freedb_search_server,255, "%s",SPLT_FREEDB2_CGI_SITE);
        }
        if (freedb_int_port != -1)
        {
          opt->freedb_search_port = freedb_int_port;
        }
      }
      else
      {
        //if we have found get
        if (get_found)
        {
          if (freedb_int_type != -1)
          {
            opt->freedb_get_type = freedb_int_type;
          }
          if (*freedb_server != '\0')
          {
            snprintf(opt->freedb_get_server,255, "%s", freedb_server);
          }
          else
          {
            snprintf(opt->freedb_get_server,255, "%s", SPLT_FREEDB2_CGI_SITE);
          }
          if (freedb_int_port != -1)
          {
            opt->freedb_get_port = freedb_int_port;
          }
        }
      }

      //if at the and something else than , or ], ambigous
      if ((*cur_pos != ',') &&
          (*cur_pos != ']') &&
          (*cur_pos != '\0'))
      {
        ambigous = SPLT_TRUE;
      }
    }
    //if we don't have anything inside the paranthesis,
    //ambigous
    if (!paranthesis_has_content)
    {
      ambigous = SPLT_TRUE;
    }
  }

  //possible search string
  int we_have_search_string = SPLT_FALSE;
  if (cur_pos != NULL)
  {
    if ((cur_pos = strchr(cur_pos, '{')))
    {
      //if we don't have }, ambigous
      if (!(end_pos = strchr(cur_pos,'}')))
      {
        ambigous = SPLT_TRUE;
      }
      else
      {
        if (end_pos-cur_pos < 2048)
        {
          snprintf(opt->freedb_arg_search_string, end_pos-cur_pos, "%s", cur_pos+1);
          opt->freedb_arg_search_string[end_pos-cur_pos-1] = '\0';
          we_have_search_string = SPLT_TRUE;
        }
      }
    }

    if (ambigous)
    {
      return ambigous;
    }
  }

  //possible get result integer from the search results
  if (cur_pos != NULL)
  {
    if ((cur_pos = strchr(cur_pos, '(')))
    {
      if (!we_have_search_string)
      {
        return ambigous;
      }
  
      //if we don't have ), ambigous
      if (!(end_pos = strchr(cur_pos,')')))
      {
        ambigous = SPLT_TRUE;
      }
      else
      {
        char chosen_int[256] = { '\0' };
        if (end_pos-cur_pos < 256)
        {
          snprintf(chosen_int, end_pos-cur_pos, "%s", cur_pos+1);
          opt->freedb_arg_result_option = atoi(chosen_int);
        }
      }
    }
  }

  return ambigous;
}

//makes the freedb search
void do_freedb_search(main_data *data)
{
  int err = SPLT_OK;
  options *opt = data->opt;
  splt_state *state = data->state;

  //we find out what search and get type we have
  char search_type[30] = "";
  char get_type[30] = "";
  if (opt->freedb_search_type == SPLT_FREEDB_SEARCH_TYPE_CDDB_CGI)
  {
    snprintf(search_type,30,"%s","cddb_cgi");
  }
  else
  {
    snprintf(search_type,30,"%s","web_search");
  }
  if (opt->freedb_get_type == SPLT_FREEDB_GET_FILE_TYPE_CDDB_CGI)
  {
    snprintf(get_type,30,"%s","cddb_cgi");
  }
  else
  {
    snprintf(get_type,30,"%s","cddb_protocol");
  }

  //print out infos about the servers
  fprintf(console_out,_(" Freedb search type: %s , Site: %s , Port: %d\n"),
      search_type,opt->freedb_search_server,opt->freedb_search_port);
  fflush(console_out);
  fprintf(console_out,_(" Freedb get type: %s , Site: %s , Port: %d\n"),
      get_type,opt->freedb_get_server,opt->freedb_get_port);
  fflush(console_out);

  char *freedb_search_string = NULL;
  char freedb_input[2048] = { '\0' };
  //if we haven't chosen to search from the arguments, interactive search
  if (opt->freedb_arg_search_string[0] == '\0')
  {
    //freedb search
    print_message(_("CDDB QUERY. Insert album and"
          " artist informations to find cd."));

    short first_time = SPLT_TRUE;
    //here we search freedb
    do {
      if (!first_time)
      {
        print_message(_("\nPlease search something ..."));
      }

      memset(freedb_input, '\0', sizeof(freedb_input));

      fprintf(console_out, "\n\t____________________________________________________________]");
      fprintf(console_out, _("\r Search: ["));

      fgets(freedb_input, 2046, stdin);

      first_time = SPLT_FALSE;

      freedb_input[strlen(freedb_input)-1] = '\0';

    } while (strlen(freedb_input)==0);

    freedb_search_string = freedb_input;
  }
  else
  {
    freedb_search_string = opt->freedb_arg_search_string;
  }

  fprintf(console_out, _("\n  Search string: %s\n"),freedb_search_string);
  fprintf(console_out, _("\nSearching from %s on port %d using %s ...\n"),
      opt->freedb_search_server,opt->freedb_search_port, search_type);
  fflush(console_out);

  //the freedb results
  const splt_freedb_results *f_results = NULL;
  //we search the freedb
  f_results = mp3splt_get_freedb_search(state, freedb_search_string,
      &err, opt->freedb_search_type,
      opt->freedb_search_server,
      opt->freedb_search_port);
  process_confirmation_error(err, data);

  //if we don't have an auto-select the result X from the arguments:
  // (query{artist}(resultX)
  //, then interactive user ask
  int selected_cd = 0;
  if (opt->freedb_arg_result_option < 0)
  {
    //print the searched informations
    print_message(_("List of found cd:"));

    int cd_number = 0;
    short end = SPLT_FALSE;
    do {
      fprintf(console_out,"%3d) %s\n",
          f_results->results[cd_number].id,
          f_results->results[cd_number].name);

      int i = 0;
      for(i = 0; i < f_results->results[cd_number].revision_number; i++)
      {
        fprintf(console_out, "  |\\=>");
        fprintf(console_out, "%3d) ", f_results->results[cd_number].id+i+1);
        fprintf(console_out, _("Revision: %d\n"), i+2);

        //break at 22
        if (((f_results->results[cd_number].id+i+2)%22)==0)
        {
          //duplicate, see below
          char junk[18];
          fprintf(console_out, _("-- 'q' to select cd, Enter for more:"));
          fflush(console_out);

          fgets(junk, 16, stdin);
          if (junk[0]=='q')
          {
            end = SPLT_TRUE;
            goto end;
          }
        }
      }

      //we read result from the char, q tu select cd or
      //enter to show more results
      if (((f_results->results[cd_number].id+1)%22)==0)
      {
        //duplicate, see ^^
        char junk[18];
        fprintf(console_out, _("-- 'q' to select cd, Enter for more: "));
        fflush(console_out);

        fgets(junk, 16, stdin);
        if (junk[0]=='q')
        {
          end = SPLT_TRUE;
          goto end;
        }
      }

end:
      if (end)
      {
        end = SPLT_FALSE;
        break;
      }

      cd_number++;
    } while (cd_number < f_results->number);

    //select the CD
    //input of the selected cd
    char sel_cd_input[1024];
    int tot = 0;
    do {
      selected_cd = 0;
      fprintf(console_out, _("Select cd #: "));
      fflush(console_out);
      fgets(sel_cd_input, 254, stdin);
      sel_cd_input[strlen(sel_cd_input)-1]='\0';
      tot = 0;

      if (sel_cd_input[tot] == '\0') 
      {
        selected_cd = -1;
      }

      while(sel_cd_input[tot] != '\0')
      {
        if (isdigit(sel_cd_input[tot++])==0)
        {
          fprintf(console_out, _("Please "));
          fflush(console_out);

          selected_cd = -1;
          break;
        }
      }

      if (selected_cd != -1) 
      {
        selected_cd = atoi(sel_cd_input);
      }

    } while ((selected_cd >= f_results->number) 
        || (selected_cd < 0));
  }
  else
  {
    selected_cd = opt->freedb_arg_result_option;
    if (selected_cd >= f_results->number)
    {
      selected_cd = 0;
    }
  }

  fprintf(console_out, _("\nGetting file from %s on port %d using %s ...\n"),
      opt->freedb_get_server,opt->freedb_get_port, get_type);
  fflush(console_out);

  //here we have the selected cd in selected_cd
  mp3splt_write_freedb_file_result(state, selected_cd,
      MP3SPLT_CDDBFILE, &err, opt->freedb_get_type,
      opt->freedb_get_server, opt->freedb_get_port);
  process_confirmation_error(err, data);
}

//prints a library message
void put_library_message(const char *message, splt_message_type mess_type)
{
  if (mess_type == SPLT_MESSAGE_INFO)
  {
    fprintf(console_out,"%s",message);
    fflush(console_out);
  }
  else if (mess_type == SPLT_MESSAGE_DEBUG)
  {
    fprintf(stderr, "%s", message);
    fflush(stderr);
  }
}

//prints the split file
void put_split_file(const char *file, int progress_data)
{
  //we put necessary spaces
  char temp[1024] = "";
  int this_spaces = strlen(file)+16;
  int counter = strlen(file)+16;
  while (counter <= progress_data)
  {
    temp[counter-this_spaces] = ' ';
    counter++;
  }
  temp[counter] = '\0';

  fprintf(console_out,_("   File \"%s\" created%s\n"),file,temp);
  fflush(console_out);
}

//prints the progress bar
void put_progress_bar(splt_progress *p_bar)
{
  char progress_text[2048] = " ";

  switch (p_bar->progress_type)
  {
    case SPLT_PROGRESS_PREPARE:
      snprintf(progress_text,2047,
          _(" preparing \"%s\" (%d of %d)"),
          p_bar->filename_shorted,
          p_bar->current_split,
          p_bar->max_splits);
      break;
    case SPLT_PROGRESS_CREATE:
      snprintf(progress_text,2047,
          _(" creating \"%s\" (%d of %d)"),
          p_bar->filename_shorted,
          p_bar->current_split,
          p_bar->max_splits);
      break;
    case SPLT_PROGRESS_SEARCH_SYNC:
      snprintf(progress_text,2047,
          _(" searching for sync errors..."));
      break;
    case SPLT_PROGRESS_SCAN_SILENCE:
      snprintf(progress_text,2047,
          _("S: %02d, Level: %.2f dB; scanning for silence..."),
          p_bar->silence_found_tracks, p_bar->silence_db_level);
      break;
    default:
      snprintf(progress_text,2047, " ");
      break;
  }

  char printed_value[2048] = "";
  //we update the progress
  if (p_bar->percent_progress <= 0.01)
  {
    snprintf(printed_value,2047," [ - %%] %s", progress_text);
  }
  else
  {
    snprintf(printed_value,2047," [ %.2f %%] %s",
        p_bar->percent_progress * 100, progress_text);
  }

  //we put necessary spaces
  char temp[2048] = "";
  int this_spaces = strlen(printed_value);
  int counter = strlen(printed_value);
  while (counter <= p_bar->user_data)
  {
    temp[counter-this_spaces] = ' ';
    counter++;
  }
  temp[counter] = '\0';

  fprintf(console_progress,"%s%s\r",printed_value,temp);
  fflush(console_progress);

  p_bar->user_data = strlen(printed_value)+1;
}

//handler for the SIGINT signal
void sigint_handler(int sig)
{
  mp3splt_stop_split(state, NULL);
  exit(1);
}

//returns the options
options *new_options(main_data *data)
{
  options *opt = my_malloc(sizeof(options), data);

  opt->T_option = SPLT_FALSE;
  opt->T_option_value = 0;
  opt->w_option = SPLT_FALSE; opt->l_option = SPLT_FALSE;
  opt->e_option = SPLT_FALSE; opt->f_option = SPLT_FALSE;
  opt->c_option = SPLT_FALSE; opt->t_option = SPLT_FALSE;
  opt->s_option = SPLT_FALSE; opt->a_option = SPLT_FALSE;
  opt->p_option = SPLT_FALSE; opt->o_option = SPLT_FALSE;
  opt->d_option = SPLT_FALSE; opt->k_option = SPLT_FALSE;
  opt->g_option = SPLT_FALSE; opt->n_option = SPLT_FALSE;
  opt->q_option = SPLT_FALSE; opt->i_option = SPLT_FALSE;
  opt->N_option = SPLT_FALSE; opt->O_option = SPLT_FALSE;
  opt->E_option = SPLT_FALSE;
  opt->P_option = SPLT_FALSE;
  opt->x_option = SPLT_FALSE;
  opt->X_option = SPLT_FALSE;
  opt->qq_option = SPLT_FALSE;
  opt->A_option = SPLT_FALSE;
  opt->m_option = SPLT_FALSE;
  opt->S_option = SPLT_FALSE;
  opt->S_option_value = 0;
  opt->cddb_arg = NULL; opt->dir_arg = NULL;
  opt->export_cue_arg = NULL;
  opt->audacity_labels_arg = NULL;
  opt->param_args = NULL;
  opt->m3u_arg = NULL;
  opt->output_format = NULL;

  opt->custom_tags = NULL;

  //we put the default values for freedb search
  //by default, CDDB_CGI (cddb.cgi) port 80 on freedb2.org
  opt->freedb_search_type = SPLT_FREEDB_SEARCH_TYPE_CDDB_CGI;
  snprintf(opt->freedb_search_server,255, "%s", SPLT_FREEDB2_CGI_SITE);
  opt->freedb_search_port = SPLT_FREEDB_CDDB_CGI_PORT;
  //we put the default values for the freedb cddb file get
  //by default, CDDB_CGI (cddb.cgi) port 80 on freedb2.org
  opt->freedb_get_type = SPLT_FREEDB_GET_FILE_TYPE_CDDB_CGI;
  snprintf(opt->freedb_get_server,255, "%s", SPLT_FREEDB2_CGI_SITE);
  opt->freedb_get_port = SPLT_FREEDB_CDDB_CGI_PORT;

  opt->freedb_arg_search_string[0] = '\0';
  opt->freedb_arg_result_option = -1;

  return opt;
}

void print_version(FILE *std)
{
  char version[128] = { '\0' };
  mp3splt_get_version(version);
  fprintf(std, PACKAGE_NAME" "VERSION" ("MP3SPLT_DATE") -"
      " %s libmp3splt %s\n",_("using"),version);
  fflush(std);
}

void print_authors(FILE *std)
{
  fprintf(std, "\t"MP3SPLT_AUTHOR1" "MP3SPLT_EMAIL1"\n\t"MP3SPLT_AUTHOR2" "MP3SPLT_EMAIL2"\n");
  fflush(std);
}

void print_no_warranty(FILE *std)
{
  fprintf(std, _("THIS SOFTWARE COMES WITH ABSOLUTELY NO WARRANTY!"
        " USE AT YOUR OWN RISK!\n"));
  fflush(std);
}

//output package, version and authors
void print_version_authors(FILE *std)
{
  print_version(std);
  print_authors(std);
  print_no_warranty(std);
}

void get_silence_level(long time, float level, void *user_data)
{
  silence_level *sl = user_data;
  if (level == INT_MIN)
  {
    sl->print_silence_level = SPLT_FALSE;
  }
  else if (level == INT_MAX)
  {
    sl->print_silence_level = SPLT_TRUE;
  }
  else
  {
    sl->level_sum += level;
    sl->number_of_levels++;
  }
}

void append_filename(main_data *data, const char *str)
{
  if (data)
  {
    if (!data->filenames)
    {
      data->filenames = my_malloc(sizeof(char *), data);
    }
    else
    {
      data->filenames = my_realloc(data->filenames, sizeof(char *) *
          (data->number_of_filenames + 1), data);
    }
    data->filenames[data->number_of_filenames] = NULL;
    if (str != NULL)
    {
      int malloc_size = strlen(str) + 1;
      data->filenames[data->number_of_filenames] = my_malloc(sizeof(char) * 
          malloc_size, data);
      snprintf(data->filenames[data->number_of_filenames],malloc_size, "%s",str);
      data->number_of_filenames++;
    }
  }
}

//returns -1 if not enough memory
void append_splitpoint(main_data *data, long value)
{
  if (data)
  {
    if (!data->splitpoints)
    {
      data->splitpoints = my_malloc(sizeof(long), data);
    }
    else
    {
      data->splitpoints = my_realloc(data->splitpoints,
          sizeof(long) * (data->number_of_splitpoints + 1), data);
    }
    data->splitpoints[data->number_of_splitpoints] = value;
    data->number_of_splitpoints++;
  }
}

#ifdef __WIN32__
char **win32_get_utf8_args(main_data *data)
{
  char **argv_utf8 = my_malloc(sizeof(char *) * data->argc, data);
  LPWSTR *argv_utf16 = NULL;
  int nArgs = 0;
  int i = 0;

  argv_utf16 = CommandLineToArgvW(GetCommandLineW(), &nArgs);
  if (argv_utf16 == NULL )
  {
    if (argv_utf8)
    {
      free(argv_utf8);
      argv_utf8 = NULL;
    }

    print_error_exit(_("CommandLineToArgvW failed (oh !)"), data);
  }
  else
  {
    for (i=0; i<nArgs; i++)
    {
      argv_utf8[i] = mp3splt_win32_utf16_to_utf8((wchar_t *)argv_utf16[i]);
      if (argv_utf8[i] == NULL)
      {
        print_error_exit(_("failed to allocate argv_utf8 memory"), data);
      }
    }

    LocalFree(argv_utf16);
    argv_utf16 = NULL;
  }

  return argv_utf8;
}
#endif

main_data *create_main_struct(int argc, char **orig_argv)
{
  main_data *data = NULL;
  data = my_malloc(sizeof(main_data), data);

  data->state = NULL;
  //alloc options
  data->opt = new_options(data);
  //alloc silence level
  data->sl = my_malloc(sizeof(silence_level), data);

  data->filenames = NULL;
  data->number_of_filenames = 0;
  data->splitpoints = NULL;
  data->number_of_splitpoints = 0;

  data->sl->level_sum = 0;
  data->sl->number_of_levels = 0;
  data->sl->print_silence_level = SPLT_TRUE;

  data->argc = argc;
#ifdef __WIN32__
  data->argv = NULL;
  data->argv = win32_get_utf8_args(data);
#else
  data->argv = orig_argv;
#endif

  return data;
}

void show_files_and_ask_for_confirmation(main_data *data)
{
  int j = 0;

  char junk[18] = { '\0' };
  print_message(_("List of found files:\n"));

  for (j = 0;j < data->number_of_filenames; j++)
  {
    fprintf(console_out, "  %s\n", data->filenames[j]);

    if (((j+1) % 22 == 0) && (j+1 < data->number_of_filenames))
    {
      fprintf(console_out, _("\n-- 'Enter' for more, 's' to split, 'c' to cancel:"));
      fflush(console_out);

      fgets(junk, 16, stdin);

      if (junk[1] == '\n')
      {
        if (junk[0] == 'c')
        {
          print_message_exit(_("\n split aborted."), data);
        }
        if (junk[0] == 's')
        {
          goto split;
        }
      }

      print_message("\n");
    }
  }

  int answer_is_correct = SPLT_FALSE;
  do {
    fprintf(console_out, _("\n-- 's' to split, 'c' to cancel:"));
    fflush(console_out);

    fgets(junk, 16, stdin);

    if (junk[1] == '\n')
    {
      if (junk[0] == 'c')
      {
        print_message_exit(_("\n split aborted."), data);
      }
      if (junk[0] == 's')
      {
        answer_is_correct = SPLT_TRUE;
      }
    }

  } while (!answer_is_correct);

  fprintf(console_out, "\n");
  fflush(console_out);

split:
  ;
}

//main program starts here
int main(int argc, char **orig_argv)
{
  setlocale(LC_ALL, "");

#ifdef ENABLE_NLS
  textdomain(MP3SPLT_GETTEXT_DOMAIN);
#endif

  console_out = stdout;
  console_err = stderr;
  console_progress = stderr;

  //possible error
  int err = SPLT_OK;

  main_data *data = create_main_struct(argc, orig_argv);

#ifdef ENABLE_NLS

# ifdef __WIN32__
  char mp3splt_uninstall_file[2048] = { '\0' };
  DWORD dwType, dwSize = sizeof(mp3splt_uninstall_file) - 1;
  SHGetValue(HKEY_LOCAL_MACHINE,
      TEXT("SOFTWARE\\mp3splt"),
      TEXT("UninstallString"),
      &dwType,
      mp3splt_uninstall_file,
      &dwSize);

  char *end = strrchr(mp3splt_uninstall_file, SPLT_DIRCHAR);
  if (end) { *end = '\0'; }

  char *executable = strdup(data->argv[0]);
  char *executable_dir = NULL;

  end = strrchr(executable, SPLT_DIRCHAR);
  if (end)
  {
    *end = '\0';
    executable_dir = executable;
  }
  else
  {
    if (mp3splt_uninstall_file[0] != '\0')
    {
      executable_dir = mp3splt_uninstall_file;
    }
  }

  if (executable_dir != NULL)
  {
    int translation_dir_length = strlen(executable_dir) + 30;
    char *translation_dir = malloc(sizeof(char) * translation_dir_length);
    if (translation_dir)
    {
      snprintf(translation_dir,translation_dir_length,
          "%s%ctranslations",executable_dir,SPLT_DIRCHAR);

      bindtextdomain(MP3SPLT_GETTEXT_DOMAIN, translation_dir);
      bindtextdomain(MP3SPLT_LIB_GETTEXT_DOMAIN, translation_dir);

      if (translation_dir)
      {
        free(translation_dir);
        translation_dir = NULL;
      }
    }
  }
  else
  {
    bindtextdomain(MP3SPLT_GETTEXT_DOMAIN, "translations");
    bindtextdomain(MP3SPLT_LIB_GETTEXT_DOMAIN, "translations");
  }
# else
  bindtextdomain(MP3SPLT_GETTEXT_DOMAIN, LOCALEDIR);
# endif

  bind_textdomain_codeset(MP3SPLT_GETTEXT_DOMAIN, "UTF-8");
#endif

  data->state = mp3splt_new_state(&err);
  process_confirmation_error(err, data);
 
  splt_state *state = data->state;
  silence_level *sl = data->sl;
  options *opt = data->opt;

  //close nicely on Ctrl+C (for example)
  signal(SIGINT, sigint_handler);

  //callback for the library messages
  mp3splt_set_message_function(state, put_library_message);
  mp3splt_set_silence_level_function(state, get_silence_level, data->sl);
  //callback for the split files
  mp3splt_set_split_filename_function(state, put_split_file);

  //default we write mins_secs_hundr for normal split
  mp3splt_set_int_option(state, SPLT_OPT_OUTPUT_FILENAMES, SPLT_OUTPUT_DEFAULT);
  mp3splt_set_int_option(state, SPLT_OPT_TAGS, SPLT_TAGS_ORIGINAL_FILE);

  //parse command line options
  int option;
  while ((option = getopt(data->argc, data->argv,
          "m:O:Dvifkwleqnasc:d:o:t:p:g:hQN12T:XxPE:A:S:")) != -1)
  {
    switch (option)
    {
      case 'x':
        mp3splt_set_int_option(state, SPLT_OPT_XING, SPLT_FALSE);
        break;
      case 'h':
        show_small_help_exit(data);
        break;
      //deprecated: use -T
      case '1':
        mp3splt_set_int_option(state, SPLT_OPT_FORCE_TAGS_VERSION, 1);
        opt->T_option_value = 1;
        opt->T_option = SPLT_TRUE;
        break;
      //deprecated: use -T
      case '2':
        mp3splt_set_int_option(state, SPLT_OPT_FORCE_TAGS_VERSION, 2);
        opt->T_option_value = 2;
        opt->T_option = SPLT_TRUE;
        break;
      case 'T':
        opt->T_option_value = atoi(optarg);
        mp3splt_set_int_option(state, SPLT_OPT_FORCE_TAGS_VERSION,
            opt->T_option_value);
        opt->T_option = SPLT_TRUE;
        break;
      case 'D':
        mp3splt_set_int_option(state, SPLT_OPT_DEBUG_MODE, SPLT_TRUE);
        break;
      case 'v':
        print_version(console_out); 
        print_authors(console_out);
        free_main_struct(&data);
        exit(0);
        break;
      case 'f':
        mp3splt_set_int_option(state, SPLT_OPT_FRAME_MODE, SPLT_TRUE);
        opt->f_option = SPLT_TRUE;
        break;
      case 'k':
        mp3splt_set_int_option(state, SPLT_OPT_INPUT_NOT_SEEKABLE, SPLT_TRUE);
        opt->k_option = SPLT_TRUE;
        break;
      case 'w':
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_WRAP_MODE);
        opt->w_option = SPLT_TRUE;
        break;
      case 'l':
        opt->l_option = SPLT_TRUE;
        console_out = console_err;
        break;
      case 'e':
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_ERROR_MODE);
        opt->e_option = SPLT_TRUE;
        break;
      case 'q':
        opt->q_option = SPLT_TRUE;
        mp3splt_set_int_option(state, SPLT_OPT_QUIET_MODE, SPLT_TRUE);
        break;
      case 'n':
        opt->n_option = SPLT_TRUE;
        break;
      case 'a':
        mp3splt_set_int_option(state, SPLT_OPT_AUTO_ADJUST, SPLT_TRUE);
        opt->a_option = SPLT_TRUE;
        break;
      case 's':
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_SILENCE_MODE);
        opt->s_option = SPLT_TRUE;
        break;
      case 'i':
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_SILENCE_MODE);
        opt->i_option = SPLT_TRUE;
        break;
      case 'c':
        mp3splt_set_int_option(state, SPLT_OPT_TAGS, SPLT_CURRENT_TAGS);
        opt->c_option = SPLT_TRUE;
        opt->cddb_arg = strdup(optarg);
        break;
      case 'P':
        opt->P_option = SPLT_TRUE;
        mp3splt_set_int_option(state, SPLT_OPT_PRETEND_TO_SPLIT, SPLT_TRUE);
        break;
      case 'E':
        opt->export_cue_arg = strdup(optarg);
        opt->E_option = SPLT_TRUE;
        break;
      case 'A':
        opt->A_option = SPLT_TRUE;
        opt->audacity_labels_arg = strdup(optarg);
        break;
      case 'm':
        opt->m_option = SPLT_TRUE;
        opt->m3u_arg = strdup(optarg);
        mp3splt_set_m3u_filename(state, opt->m3u_arg);
        break;
      case 'S':
        opt->S_option = SPLT_TRUE;
        opt->S_option_value = atoi(optarg);
        mp3splt_set_int_option(state, SPLT_OPT_LENGTH_SPLIT_FILE_NUMBER, opt->S_option_value);
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_LENGTH_MODE);
        break;
      case 'd':
        opt->dir_arg = strdup(optarg);
        opt->d_option = SPLT_TRUE;
        break;
      case 'N':
        opt->N_option = SPLT_TRUE;
        break;
      case 'o':
        mp3splt_set_int_option(state, SPLT_OPT_OUTPUT_FILENAMES, SPLT_OUTPUT_FORMAT);
        if (optarg)
        {
          opt->output_format = strdup(optarg);
          if (!opt->output_format)
          {
            print_error_exit(_("cannot allocate memory !"),data);
          }
        }

        //if the split result must be written to stdout
        if (strcmp(optarg,"-") == 0)
        {
          console_out = stderr;
        }
        opt->o_option = SPLT_TRUE;
        break;
      case 'O':
        opt->O_option = SPLT_TRUE;
        mp3splt_set_long_option(state, SPLT_OPT_OVERLAP_TIME,
            c_hundreths(optarg));
        break;
      case 'X':
        opt->X_option = SPLT_TRUE;
        break;
      case 't':
        mp3splt_set_int_option(state, SPLT_OPT_SPLIT_MODE, SPLT_OPTION_TIME_MODE);
        float converted_time = c_hundreths(optarg);

        if (converted_time != -1)
        {
          float split_time = converted_time / 100.0;
          mp3splt_set_float_option(state, SPLT_OPT_SPLIT_TIME, split_time);
        }
        else
        {
          print_error_exit(_("bad time expression for the time split.\n"
                "\tMust be min.sec, read man page for details."), data);
        }
        opt->t_option = SPLT_TRUE;
        break;
      case 'p':
        opt->p_option = SPLT_TRUE;
        opt->param_args = strdup(optarg);
        break;
      case 'g':
        if (opt->custom_tags)
        {
          free(opt->custom_tags);
          opt->custom_tags = NULL;
        }
        mp3splt_set_int_option(state, SPLT_OPT_TAGS, SPLT_CURRENT_TAGS);
        if (optarg)
        {
          if (optarg[0] == 'r' && strlen(optarg) > 1)
          {
            opt->custom_tags = strdup(optarg+1);
            mp3splt_set_int_option(state, SPLT_OPT_REPLACE_TAGS_IN_TAGS, SPLT_TRUE);
          }
          else
          {
            opt->custom_tags = strdup(optarg);
          }
        }
        else
        {
          opt->custom_tags = NULL;
        }
        opt->g_option = SPLT_TRUE;
        break;
      case 'Q':
        opt->q_option = SPLT_TRUE;
        mp3splt_set_int_option(state, SPLT_OPT_QUIET_MODE, SPLT_TRUE);
        opt->qq_option = SPLT_TRUE;
        console_progress = stdout;
        fclose(stdout);
        break;
      default:
        print_error_exit(_("read man page for documentation"
              " or type 'mp3splt -h'."), data);
        break;
    }
  }

  //callback for the progress bar
  if (!opt->q_option && !opt->X_option)
  {
    mp3splt_set_progress_function(state, put_progress_bar);
  }

  //if quiet, does not write authors and other
  if (!opt->q_option && !opt->X_option)
  {
    print_version_authors(console_err);
  }

  //if -n option, set no tags whatever happends
  if (opt->n_option)
  {
    mp3splt_set_int_option(state, SPLT_OPT_TAGS, SPLT_NO_TAGS);
  }

  err = SPLT_OK;

  //add special directory search for plugins on Windows
#ifdef __WIN32__
  if (executable != NULL)
  {
    if (executable[0] != '\0')
    {
      mp3splt_append_plugins_scan_dir(state, executable);
    }
    free(executable);
    executable = NULL;
  }

  //also add the installation directory that we take from the registry
  if (mp3splt_uninstall_file[0] != '\0')
  {
    mp3splt_append_plugins_scan_dir(state, mp3splt_uninstall_file);
  }
#endif

  //after getting the options (especially the debug option), find plugins
  err = mp3splt_find_plugins(state);
  process_confirmation_error(err, data);

  //if we have parameter options
  if (opt->p_option)
  {
    float th = -200,off = -200,min = -200;
    int gap = -200,nt = -200,rm = -200;
    int parsed_p_options = parse_arg(opt->param_args,&th,&gap,&nt,&off,&rm,&min);
    if (parsed_p_options < 1)
    {
      print_error_exit(_("bad argument for -p option. No valid value"
            " was recognized !"), data);
    }

    //threshold
    if (th != -200)
    {
      mp3splt_set_float_option(state, SPLT_OPT_PARAM_THRESHOLD, th);
    }
    //offset
    if (off != -200)
    {
      mp3splt_set_float_option(state, SPLT_OPT_PARAM_OFFSET, off);
    }
    //min
    if (min != -200)
    {
      mp3splt_set_float_option(state, SPLT_OPT_PARAM_MIN_LENGTH, min);
    }
    //gap
    if (gap != -200)
    {
      mp3splt_set_int_option(state, SPLT_OPT_PARAM_GAP, gap);
    }
    //number of tracks
    if (nt != -200)
    {
      mp3splt_set_int_option(state, SPLT_OPT_PARAM_NUMBER_TRACKS, nt);
    }
    //remove silence (rm)
    if (rm != -200)
    {
      mp3splt_set_int_option(state, SPLT_OPT_PARAM_REMOVE_SILENCE, rm);
    }
  }

  int output_format_error = SPLT_OK;
  if (opt->o_option)
  {
    //enable to create directories from the output filenames
    mp3splt_set_int_option(state, SPLT_OPT_CREATE_DIRS_FROM_FILENAMES, SPLT_TRUE);
    mp3splt_set_oformat(state, opt->output_format,&output_format_error);
    process_confirmation_error(output_format_error, data);
  }

  if (optind > 1)
  {
    data->argv = rmopt(data->argv, optind, data->argc);
    data->argc -= optind-1;
  }

  //check arguments
  check_args(argc, data);

  //enable/disable logging the silence splitpoints in a file
  mp3splt_set_int_option(state, SPLT_OPT_ENABLE_SILENCE_LOG, ! opt->N_option);

  //silence splitpoints log filename
  if (! opt->N_option)
  {
    mp3splt_set_silence_log_filename(state, "mp3splt.log");
    process_confirmation_error(err, data);
  }

  int i = 0;
  data->filenames = NULL;
  data->number_of_filenames = 0;
  data->splitpoints = NULL;
  data->number_of_splitpoints = 0;

  int we_had_directory_as_argument = SPLT_FALSE;

  char *argument = NULL;
  //we get out the filenames and the splitpoints from the left arguments
  for (i=1; i < data->argc; i++)
  {
    argument = data->argv[i];
    long hundreths = c_hundreths(argument);
    if (hundreths != -1)
    {
      append_splitpoint(data, hundreths);
    }
    else
    {
      if (mp3splt_u_check_if_directory(argument))
      {
        we_had_directory_as_argument = SPLT_TRUE;

        int num_of_files_found = 0;
        char **found_files =
          mp3splt_find_filenames(state, argument, &num_of_files_found, &err);
        int k = 0;
        for (k = 0;k < num_of_files_found; k++)
        {
          char *current_fname = found_files[k];
          
          append_filename(data, current_fname);

          if (found_files[k])
          {
            free(found_files[k]);
            found_files[k] = NULL;
          }
        }
        if (found_files)
        {
          free(found_files);
          found_files = NULL;
        }
        num_of_files_found = 0;
        process_confirmation_error(err, data);
      }
      else
      {
        append_filename(data, argument);
      }
    }
  }

  //if we have a normal split, we need to parse the splitpoints
  int normal_split = SPLT_FALSE;
  if (!opt->l_option && !opt->i_option && !opt->c_option &&
      !opt->e_option && !opt->t_option && !opt->w_option &&
      !opt->s_option && !opt->A_option && !opt->S_option)
  {
    if (data->number_of_splitpoints < 2)
    {
      process_confirmation_error(SPLT_ERROR_SPLITPOINTS, data);
    }
    normal_split = SPLT_TRUE;
  }

  int j = 0;

  if (data->number_of_filenames <= 0)
  {
    print_error_exit(_("no input filename(s)."), data);
  }

  if (data->number_of_filenames > 1)
  {
    fprintf(console_out,"\n");
    fflush(console_out);
  }

  if (opt->output_format && (strcmp(opt->output_format, "-") == 0))
  {
    if (data->number_of_splitpoints > 2)
    {
      print_warning(_("multiple splitpoints with stdout !"));
    }
    else if (opt->t_option)
    {
      print_warning(_("using time mode with stdout !"));
    }
  }

  if (!opt->q_option && we_had_directory_as_argument)
  {
    show_files_and_ask_for_confirmation(data);
  }

  //split all the filenames
  for (j = 0;j < data->number_of_filenames; j++)
  {
    char *current_filename = data->filenames[j];

    sl->level_sum = 0;
    sl->number_of_levels = 0;
    err = SPLT_OK;

    if (opt->P_option)
    {
      fprintf(console_out,_(" Pretending to split file '%s' ...\n"),current_filename);
    }
    else
    {
      fprintf(console_out,_(" Processing file '%s' ...\n"),current_filename);
    }
    fflush(console_out);

    if ((strcmp(current_filename, "-") == 0 || strcmp(current_filename, "o-") == 0) &&
        we_have_incompatible_stdin_option(opt))
    {
      print_error_exit(_("cannot use -k option (or STDIN) with"
            " one of the following options: -S -s -w -l -e -i -a -p"), data);
    }

    //we put the filename
    err = mp3splt_set_filename_to_split(state, current_filename);
    process_confirmation_error(err, data);

    //if we list wrap files
    if (opt->l_option)
    {
      //if no error when putting the filename to split
      const splt_wrap *wrap_files;
      wrap_files = mp3splt_get_wrap_files(state,&err);
      process_confirmation_error(err, data);

      //if no error when getting the wrap files
      int wrap_files_number = wrap_files->wrap_files_num;
      int i = 0;
      fprintf(stdout,"\n");
      for (i = 0;i < wrap_files_number;i++)
      {
        fprintf(stdout,"%s\n",wrap_files->wrap_files[i]);
      }
      fprintf(stdout,"\n");
      fflush(stdout);
    }
    else
    {
      //count how many silence splitpoints we have
      //if we count how many silence splitpoints
      if (opt->i_option)
      {
        err = SPLT_OK;
        mp3splt_count_silence_points(state, &err);
        process_confirmation_error(err, data);
      }
      else
      //if we don't list wrapped files and we don't count silence files
      {
        //if we have cddb option
        if (opt->c_option)
        {
          //we get the filename
          if ((strstr(opt->cddb_arg, ".cue")!=NULL)||
              (strstr(opt->cddb_arg, ".CUE")!=NULL))
          {
            //we have the cue filename in cddb_arg
            //here we get cue splitpoints
            mp3splt_put_cue_splitpoints_from_file(state, opt->cddb_arg, &err);
            process_confirmation_error(err, data);
          }
          else
          {
            //if we have a freedb search
            if (strncmp(opt->cddb_arg, "query", 5)==0)
            {
              //only do freedb search for the first time
              if (j == 0)
              {
                int ambigous = parse_query_arg(opt,opt->cddb_arg);
                if (ambigous)
                {
                  print_warning(_("freedb query format ambigous !"));
                }
                do_freedb_search(data);
              }

              //we get the splitpoints from the file
              mp3splt_put_cddb_splitpoints_from_file(state, MP3SPLT_CDDBFILE, &err);
              process_confirmation_error(err, data);
            }
            else
              //here we have cddb file
            {
              mp3splt_put_cddb_splitpoints_from_file(state, opt->cddb_arg, &err);
              process_confirmation_error(err, data);
            }
          }
        }
        else if (opt->audacity_labels_arg)
        {
          mp3splt_put_audacity_labels_splitpoints_from_file(state,
              opt->audacity_labels_arg, &err);
          process_confirmation_error(err, data);
        } else if (normal_split)
        {
          //we set the splitpoints to the library
          for (i = 0;i < data->number_of_splitpoints; i++)
          {
            long point = data->splitpoints[i];
            err = mp3splt_append_splitpoint(state, point, NULL, SPLT_SPLITPOINT);
            process_confirmation_error(err, data);
          }
        }

        //we set the path of split for the -d option
        if (opt->d_option)
        {
          err = mp3splt_set_path_of_split(state, opt->dir_arg);
          process_confirmation_error(err, data);
        }

        if (opt->g_option && (opt->custom_tags != NULL))
        {
          int ambiguous = mp3splt_put_tags_from_string(state, opt->custom_tags, &err);
          process_confirmation_error(err, data);
          if (ambiguous)
          {
            print_warning(_("tags format ambiguous !"));
          }
        }

        //for cddb, filenames are already set from the library, so 
        //set output filenames to CUSTOM
        int saved_output_filenames = mp3splt_get_int_option(state, SPLT_OPT_OUTPUT_FILENAMES, &err);
        if ((opt->c_option || opt->A_option) && !opt->o_option)
        {
          mp3splt_set_int_option(state, SPLT_OPT_OUTPUT_FILENAMES, SPLT_OUTPUT_CUSTOM);
        }

        //we do the effective split
        err = mp3splt_split(state);
        process_confirmation_error(err, data);

        //for cddb, set output filenames to its old value before the split
        if (opt->c_option && !opt->o_option)
        {
          mp3splt_set_int_option(state, SPLT_OPT_OUTPUT_FILENAMES, saved_output_filenames);
        }

        //print the average silence level
        if (opt->s_option)
        {
          if (sl->print_silence_level)
          {
            if (sl->number_of_levels != 0)
            {
              float average_silence_levels = sl->level_sum / (double) sl->number_of_levels;
              char message[256] = { '\0' };
              snprintf(message,256,
                  _(" Average silence level: %.2f dB"), average_silence_levels);
              print_message(message);
            }
          }
        }
      }
    }

    if (opt->E_option)
    {
      err = SPLT_OK;
      mp3splt_export_to_cue(state, opt->export_cue_arg, SPLT_TRUE, &err);
      process_confirmation_error(err, data);
    }

    if (opt->c_option && err >= 0 && !opt->q_option)
    {
      print_message(_("\n +-----------------------------------------------------------------------------+\n"
            " |NOTE: When you use cddb/cue, split files might be not very precise due to:|\n"
            " |1) Who extracts CD tracks might use \"Remove silence\" option. This means that |\n"
            " |   the large mp3 file is shorter than CD Total time. Never use this option.  |\n"
            " |2) Who burns CD might add extra pause seconds between tracks.  Never do it.  |\n"
            " |3) Encoders might add some padding frames so  that  file is longer than CD.  |\n"
            " |4) There are several entries of the same cd on CDDB, find the best for yours.|\n"
            " |   Usually you can find the correct splitpoints, so good luck!  |\n"
            " +-----------------------------------------------------------------------------+\n"
            " | TRY TO ADJUST SPLITS POINT WITH -a OPTION. Read man page for more details!  |\n"
            " +-----------------------------------------------------------------------------+\n"));
    }

    //next file
    if (data->number_of_filenames > 1)
    {
      fprintf(console_out,"\n");
      fflush(console_out);
    }

    //erase the previous splitpoints
    err = SPLT_OK;
    mp3splt_erase_all_tags(state, &err);
    process_confirmation_error(err, data);
    err = SPLT_OK;
    mp3splt_erase_all_splitpoints(state,&err);
    process_confirmation_error(err, data);
  }

  free_main_struct(&data);

  return 0;
}

