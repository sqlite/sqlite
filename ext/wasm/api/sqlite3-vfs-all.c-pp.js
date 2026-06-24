/*
  2026-06-23

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  Aggregate module for the optional sqlite3_vfs-related JS pieces.
  It preserves the historical "load one file to get all VFS pieces"
  behavior, while the individual files remain standalone, tree-shakable
  modules.
*/
//#if target:standalone-module
import {sqlite3VfsHelperInitializer} from "./sqlite3-vfs-helper.js";
import {sqlite3KvvfsInitializer} from "./sqlite3-vfs-kvvfs.js";
import {sqlite3OpfsCommonInitializer} from "./sqlite3-opfs-common.js";
import {sqlite3OpfsVfsInitializer} from "./sqlite3-vfs-opfs.js";
import {sqlite3OpfsSAHPoolVfsInitializer}
  from "./sqlite3-vfs-opfs-sahpool.js";
import {sqlite3OpfsWlVfsInitializer} from "./sqlite3-vfs-opfs-wl.js";
//#/if target:standalone-module

const sqlite3VfsInitializers = [
  sqlite3VfsHelperInitializer,
  sqlite3KvvfsInitializer,
  sqlite3OpfsCommonInitializer,
  sqlite3OpfsVfsInitializer,
  sqlite3OpfsSAHPoolVfsInitializer,
  sqlite3OpfsWlVfsInitializer
];

const sqlite3VfsInitializer = function(sqlite3){
  const promises = [];
  const asyncInitializers = globalThis.sqlite3ApiBootstrap?.initializersAsync;
  for(const f of sqlite3VfsInitializers){
    const rc = (
      f===sqlite3OpfsVfsInitializer || f===sqlite3OpfsWlVfsInitializer
    ) ? f(sqlite3, asyncInitializers) : f(sqlite3);
    if( rc?.then instanceof Function ){
      promises.push(rc);
    }
  }
  return promises.length ? Promise.all(promises).then(()=>sqlite3) : sqlite3;
};

if( globalThis.sqlite3ApiBootstrap?.initializers ){
  globalThis.sqlite3ApiBootstrap.initializers.push(sqlite3VfsInitializer);
}

//#if target:standalone-module
export {
  sqlite3VfsInitializers,
  sqlite3VfsInitializer,
  sqlite3VfsHelperInitializer,
  sqlite3KvvfsInitializer,
  sqlite3OpfsCommonInitializer,
  sqlite3OpfsVfsInitializer,
  sqlite3OpfsSAHPoolVfsInitializer,
  sqlite3OpfsWlVfsInitializer
};
//#/if target:standalone-module
