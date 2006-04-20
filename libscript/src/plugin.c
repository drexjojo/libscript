
#define _GNU_SOURCE
#include <stdio.h>
#include <malloc.h>
#include <ltdl.h>
#include <string.h>

#include "libscript.h"
#include "internals.h"
#include "plugin.h"

#ifndef SCRIPT_NAME_LEN
#define SCRIPT_NAME_LEN 1024
#endif

/**
 * @return a handle to the symbol on success, NULL on error.
 */
static lt_dlhandle get_symbol(script_plugin* plugin, char* name) {
   char symname[SCRIPT_NAME_LEN + 1];
   snprintf(symname, SCRIPT_NAME_LEN, "script_plugin_%s_%s", name, plugin->extension);
   return lt_dlsym(plugin->dlhandle, symname);
}

static script_err free_plugin(script_env* env, script_plugin* plugin) {
   int err = 0;
   script_plugin_done_fn done_fn;

   if (plugin->dlhandle) {
      done_fn = (script_plugin_done_fn) get_symbol(plugin, "done");
      if (done_fn)
         done_fn(plugin->state);
      
      err = lt_dlclose(plugin->dlhandle);
   }
   free(plugin->extension);
   free(plugin);
   script_check(err != 0, SCRIPT_ERRDLCLOSE);
   return SCRIPT_OK;
}

/**
 * @return always NULL.
 */
static void* fail_plugin(script_env* env, script_plugin* plugin, script_err error) {
   free_plugin(env, plugin);
   env->error = error;
   return NULL;
}

/**
 * @return a handle to the plugin on success, NULL on error (setting env->error).
 */
script_plugin* script_plugin_load(script_env* env, const char* extension) {
   script_plugin* plugin;
   char name[SCRIPT_NAME_LEN + 1];
   lt_dlhandle dlhandle;
   script_plugin_init_fn init_fn;
   ht_key key;
   
   key.str = extension;
   /* FIXME: different envs can try to load the same library multiple times.
      Works for dlopen, but it's better not to rely on this. */
   plugin = ht_get(env->plugins, key);
   if (plugin)
      return plugin;

   plugin = calloc(sizeof(script_plugin), 1);
   plugin->extension = strdup(extension);
   snprintf(name, SCRIPT_NAME_LEN, "libscript-%s", extension);

   dlhandle = lt_dlopenext(name);
   if (! dlhandle) return fail_plugin(env, plugin, SCRIPT_ERRDLOPEN);

   plugin->dlhandle = dlhandle;
   init_fn = (script_plugin_init_fn) get_symbol(plugin, "init");
   if (!init_fn) return fail_plugin(env, plugin, SCRIPT_ERRDLINVALID);
   plugin->state = init_fn(env, env->namespace);
   if (!plugin->state) return fail_plugin(env, plugin, SCRIPT_ERRLANG);
   plugin->run = (script_plugin_run_fn) get_symbol(plugin, "run");
   if (!plugin->run) return fail_plugin(env, plugin, SCRIPT_ERRDLINVALID);
   plugin->call = (script_plugin_call_fn) get_symbol(plugin, "call");
   if (!plugin->call) return fail_plugin(env, plugin, SCRIPT_ERRDLINVALID);

   ht_put(env->plugins, key, plugin);

   return plugin;
}

script_err script_plugin_run(script_env* env, script_plugin* plugin, const char* code) {
   script_err err;
   
   if (!env || !plugin || !code)
      return SCRIPT_ERRAPI;
   err = plugin->run(plugin->state, code);
   env->error = err;
   return err;
}

script_err script_plugin_call(script_env* env, script_plugin* plugin, const char* fn) {
   if (!env || !plugin || !fn)
      return SCRIPT_ERRAPI;
   return plugin->call(plugin->state, fn);
}

script_err script_plugin_unload(script_env* env, script_plugin* plugin) {
   ht_key key;
   
   key.str = plugin->extension;
   plugin = ht_remove(env->plugins, key);
   script_check(!plugin, SCRIPT_ERRFILENOTFOUND);
   return free_plugin(env, plugin);
}
