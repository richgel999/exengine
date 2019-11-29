/* ini
  
*/
#ifndef EX_INI_H
#define EX_INI_H

#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef enum {
  ex_ini_type_undefined,
  ex_ini_type_string,
  ex_ini_type_float,
} ex_ini_type_e;

typedef struct {
  char key[256];
  ex_ini_type_e type;
  union {
    char s[256];
    float f;
  };
} ex_ini_var_t;

typedef struct {
  ex_ini_var_t vars[256];
  char name[256];
  int length;
} ex_ini_section_t;

typedef struct {
  ex_ini_section_t sections[256];
  int length, success;
} ex_ini_t;

/**
 * [ex_ini_load load or update from an ini file]
 * @param  ini  [ini instance to use]
 * @param  path [file path]
 * @return      [non-zero upon success]
 */
int ex_ini_load(ex_ini_t *ini, const char *path);

/**
 * [ex_ini_get_var get a key-value variable]
 * @param  ini [ini instance to use]
 * @param  sec [variable section]
 * @param  key [variable key]
 * @return     [variable struct]
 */
ex_ini_var_t *ex_ini_get_var(ex_ini_t *ini, const char *sec, const char *key);

/**
 * [ex_ini_get_string get a string variable]
 * @param  ini [ini instance to use]
 * @param  sec [variable section]
 * @param  key [variable key]
 * @return     [string pointer]
 */
char *ex_ini_get_string(ex_ini_t *ini, const char *sec, const char *key);

/**
 * [ex_ini_get_float get a float variable]
 * @param  ini [ini instance to use]
 * @param  sec [variable section]
 * @param  key [variable key]
 * @return     [float value]
 */
float ex_ini_get_float(ex_ini_t *ini, const char *sec, const char *key);

#endif // EX_INI_H