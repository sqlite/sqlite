'use strict';
const urlArgs = new URL(globalThis.location.href).searchParams;
const options = {
  workerName: urlArgs.get('workerId') || Math.round(Math.random()*10000),
  unlockAsap: urlArgs.get('opfs-unlock-asap') || 0,
  vfs: urlArgs.get('vfs')
};
const jsSqlite = urlArgs.get('sqlite3.dir')
  +'/sqlite3.js?opfs-async-proxy-id='
      +options.workerName;
importScripts(jsSqlite)/*Sigh - URL args are not propagated this way*/;
//const sqlite3InitModule = (await import(jsSqlite)).default;
globalThis.sqlite3InitModule.__isUnderTest =
  true /* causes sqlite3.opfs to be retained */;
globalThis.sqlite3InitModule().then(async function(sqlite3){
  const wPost = (type,...payload)=>{
    postMessage({type, worker: options.workerName, payload});
  };
  const stdout = (...args)=>wPost('stdout',...args);
  const stderr = (...args)=>wPost('stderr',...args);
  if(!sqlite3.opfs){
    stderr("This code requires the (private) sqlite3.opfs object. Aborting.");
    return;
  }

  const wait = async (ms)=>{
    return new Promise((resolve)=>setTimeout(resolve,ms));
  };

  const dbName = 'concurrency-tester.db';
  if(urlArgs.has('unlink-db')){
    await sqlite3.opfs.unlink(dbName);
    stdout("Unlinked",dbName);
  }
  wPost('loaded');
  let db;
  const interval = Object.assign(Object.create(null),{
    delay: urlArgs.has('interval') ? (+urlArgs.get('interval') || 750) : 750,
    handle: undefined,
    count: 0
  });
  const finish = ()=>{
    if(db?.pointer){
      db.close();
    }
    if(interval.error){
      stderr("Ending work at interval #"+interval.count,
             "due to error:", interval.error);
      wPost('failed', "at interval #"+interval.count,
            "due to error:",interval.error);
    }else{
      wPost('finished',"Ending work after",interval.count,"intervals.");
    }
  };
  const run = async function(){
    const Ctors = Object.assign(Object.create(null),{
      opfs: sqlite3.oo1.OpfsDb,
      'opfs-wl': sqlite3.oo1.OpfsWlDb
    });
    const ctor = Ctors[options.vfs];
    if( !ctor ){
      stderr("Invalid VFS name:",vfs);
      return;
    }
    stdout("ctor:", options.vfs);
    while(true){
      try{
        db = new ctor({
          filename: 'file:'+dbName+'?opfs-unlock-asap='+options.unlockAsap,
          flags: 'c'
        });
        break;
      }catch(e){
        if(e instanceof sqlite3.SQLite3Error
           && sqlite3.capi.SQLITE_BUSY===e.resultCode){
          stderr("Retrying open() for BUSY:",e.message);
          continue;
        }
        throw e;
      }
    }
    sqlite3.capi.sqlite3_busy_timeout(db.pointer, 5000);
    sqlite3.capi.sqlite3_js_retry_busy(-1, ()=>{
      db.transaction((db)=>{
        db.exec([
          "create table if not exists t1(w TEXT UNIQUE ON CONFLICT REPLACE,v);",
          "create table if not exists t2(w TEXT UNIQUE ON CONFLICT REPLACE,v);"
        ]);
      });
    });

    const maxIterations =
          urlArgs.has('iterations') ? (+urlArgs.get('iterations') || 10) : 10;
    stdout("Starting",maxIterations,"interval-based db updates with delay of",interval.delay,"ms.");
    const doWork = async ()=>{
      const tm = new Date().getTime();
      ++interval.count;
      const prefix = "v(#"+interval.count+")";
      stdout("Setting",prefix,"=",tm);
      try{
        sqlite3.capi.sqlite3_js_retry_busy(-1, ()=>{
          db.exec({
            sql:"INSERT OR REPLACE INTO t1(w,v) VALUES(?,?)",
            bind: [options.workerName, new Date().getTime()]
          });
        }, (n)=>{
          stderr("Attempt #",n,"for interval #",interval.count,"due to BUSY");
        });
      }catch(e){
        stderr("Error: ",e.message);
        interval.error = e;
        throw e;
      }
      //stdout("doWork()",prefix,"error ",interval.error);
    };
    setTimeout(async function timer(){
      await doWork();
      if(interval.error || maxIterations === interval.count){
        finish();
      }else{
        const jitter = (Math.random()>=0.5)
              ? ((Math.random()*100 - Math.random()*100) | 0)
              : 0;
        const n = interval.delay + jitter;
        setTimeout(timer, n<50 ? interval.delay : n);
      }
    }, interval.delay);
  }/*run()*/;

  globalThis.onmessage = function({data}){
    switch(data.type){
      case 'run':
        run().catch((e)=>{
          if(!interval.error) interval.error = e;
        }).catch(e=>{
          /* Don't do this in finally() - it fires too soon. */
          finish();
          throw e;
        });
        break;
      default:
        stderr("Unhandled message type '"+data.type+"'.");
        break;
    }
  };
});
