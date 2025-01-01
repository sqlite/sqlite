/*
  2022-08-23

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  Demonstration of the sqlite3 Worker API #1 Promiser: a Promise-based
  proxy for for the sqlite3 Worker #1 API.
*/
//#if target=es6-module
import {default as promiserFactory} from "./jswasm/sqlite3-worker1-promiser.mjs";
//#else
"use strict";
const promiserFactory = globalThis.sqlite3Worker1Promiser.v2;
delete globalThis.sqlite3Worker1Promiser;
//#endif
(async function(){
  const T = globalThis.SqliteTestUtil;
  const eOutput = document.querySelector('#test-output');
  const warn = console.warn.bind(console);
  const error = console.error.bind(console);
  const log = console.log.bind(console);
  const logHtml = async function(cssClass,...args){
    log.apply(this, args);
    const ln = document.createElement('div');
    if(cssClass) ln.classList.add(cssClass);
    ln.append(document.createTextNode(args.join(' ')));
    eOutput.append(ln);
  };

  let startTime;
  const testCount = async ()=>{
    logHtml("","Total test count:",T.counter+". Total time =",(performance.now() - startTime),"ms");
  };

  const promiserConfig = {
//#ifnot target=es6-module
    /**
       The v1 interfaces uses an onready function. The v2 interface optionally
       accepts one but does not require it. If provided, it is called _before_
       the promise is resolved, and the promise is rejected if onready() throws.
    */
    onready: function(f){
      /* f === the function returned by promiserFactory().
         Ostensibly (f === workerPromise) but this function is
         called before the promiserFactory() Promise resolves, so
         before workerPromise is set. */
      console.warn("This is the v2 interface - you don't need an onready() function.");
    },
//#endif
    debug: 1 ? undefined : (...args)=>console.debug('worker debug',...args),
    onunhandled: function(ev){
      error("Unhandled worker message:",ev.data);
    },
    onerror: function(ev){
      error("worker1 error:",ev);
    }
  };
  const workerPromise = await promiserFactory(promiserConfig)
        .then((func)=>{
          console.log("Init complete. Starting tests momentarily.");
          globalThis.sqlite3TestModule.setStatus(null)/*hide the HTML-side is-loading spinner*/;
          return func;
        });

  const wtest = async function(msgType, msgArgs, callback){
    if(2===arguments.length && 'function'===typeof msgArgs){
      callback = msgArgs;
      msgArgs = undefined;
    }
    const p = 1
          ? workerPromise({type: msgType, args:msgArgs})
          : workerPromise(msgType, msgArgs);
    return callback ? p.then(callback).finally(testCount) : p;
  };

  let sqConfig;
  const runTests = async function(){
    const dbFilename = '/testing2.sqlite3';
    startTime = performance.now();

    await wtest('config-get', (ev)=>{
      const r = ev.result;
      log('sqlite3.config subset:', r);
      T.assert('boolean' === typeof r.bigIntEnabled);
      sqConfig = r;
    });
    logHtml('',
            "Sending 'open' message and waiting for its response before continuing...");

    await wtest('open', {
      filename: dbFilename,
      simulateError: 0 /* if true, fail the 'open' */,
    }, function(ev){
      const r = ev.result;
      log("then open result",r);
      T.assert(ev.dbId === r.dbId)
        .assert(ev.messageId)
        .assert('string' === typeof r.vfs);
      promiserConfig.dbId = ev.dbId;
    }).then(runTests2);
  };

  const runTests2 = async function(){
    const mustNotReach = ()=>toss("This is not supposed to be reached.");

    await wtest('exec',{
      sql: ["create table t(a,b)",
            "insert into t(a,b) values(1,2),(3,4),(5,6)"
           ].join(';'),
      resultRows: [], columnNames: [],
      countChanges: sqConfig.bigIntEnabled ? 64 : true
    }, function(ev){
      ev = ev.result;
      T.assert(0===ev.resultRows.length)
        .assert(0===ev.columnNames.length)
        .assert(sqConfig.bigIntEnabled
                ? (3n===ev.changeCount)
                : (3===ev.changeCount));
    });

    await wtest('exec',{
      sql: 'select a a, b b from t order by a',
      resultRows: [], columnNames: [],
    }, function(ev){
      ev = ev.result;
      T.assert(3===ev.resultRows.length)
        .assert(1===ev.resultRows[0][0])
        .assert(6===ev.resultRows[2][1])
        .assert(2===ev.columnNames.length)
        .assert('b'===ev.columnNames[1]);
    });

    await wtest('exec',{
      sql: 'select a a, b b from t order by a',
      resultRows: [], columnNames: [],
      rowMode: 'object',
      countChanges: true
    }, function(ev){
      ev = ev.result;
      T.assert(3===ev.resultRows.length)
        .assert(1===ev.resultRows[0].a)
        .assert(6===ev.resultRows[2].b)
        .assert(0===ev.changeCount);
    });

    await wtest(
      'exec',
      {sql:'intentional_error'},
      mustNotReach
    ).catch((e)=>{
      warn("Intentional error:",e);
    });

    await wtest('exec',{
      sql:'select 1 union all select 3',
      resultRows: []
    }, function(ev){
      ev = ev.result;
      T.assert(2 === ev.resultRows.length)
        .assert(1 === ev.resultRows[0][0])
        .assert(3 === ev.resultRows[1][0])
        .assert(undefined === ev.changeCount);
    });

    const resultRowTest1 = function f(ev){
      if(undefined === f.counter) f.counter = 0;
      if(null === ev.rowNumber){
        /* End of result set. */
        T.assert(undefined === ev.row)
          .assert(2===ev.columnNames.length)
          .assert('a'===ev.columnNames[0])
          .assert('B'===ev.columnNames[1]);
      }else{
        T.assert(ev.rowNumber > 0);
        ++f.counter;
      }
      log("exec() result row:",ev);
      T.assert(null === ev.rowNumber || 'number' === typeof ev.row.B);
    };
    await wtest('exec',{
      sql: 'select a a, b B from t order by a limit 3',
      callback: resultRowTest1,
      rowMode: 'object'
    }, function(ev){
      T.assert(3===resultRowTest1.counter);
      resultRowTest1.counter = 0;
    });

    const resultRowTest2 = function f(ev){
      if(null === ev.rowNumber){
        /* End of result set. */
        T.assert(undefined === ev.row)
          .assert(1===ev.columnNames.length)
          .assert('a'===ev.columnNames[0])
      }else{
        T.assert(ev.rowNumber > 0);
        f.counter = ev.rowNumber;
      }
      log("exec() result row:",ev);
      T.assert(null === ev.rowNumber || 'number' === typeof ev.row);
    };
    await wtest('exec',{
      sql: 'select a a from t limit 3',
      callback: resultRowTest2,
      rowMode: 0
    }, function(ev){
      T.assert(3===resultRowTest2.counter);
    });

    const resultRowTest3 = function f(ev){
      if(null === ev.rowNumber){
        T.assert(3===ev.columnNames.length)
          .assert('foo'===ev.columnNames[0])
          .assert('bar'===ev.columnNames[1])
          .assert('baz'===ev.columnNames[2]);
      }else{
        f.counter = ev.rowNumber;
        T.assert('number' === typeof ev.row);
      }
    };
    await wtest('exec',{
      sql: "select 'foo' foo, a bar, 'baz' baz  from t limit 2",
      callback: resultRowTest3,
      columnNames: [],
      rowMode: '$bar'
    }, function(ev){
      log("exec() result row:",ev);
      T.assert(2===resultRowTest3.counter);
    });

    await wtest('exec',{
      sql:[
        'pragma foreign_keys=0;',
        // ^^^ arbitrary query with no result columns
        'select a, b from t order by a desc; select a from t;'
        // exec() only honors SELECT results from the first
        // statement with result columns (regardless of whether
        // it has any rows).
      ],
      rowMode: 1,
      resultRows: []
    },function(ev){
      const rows = ev.result.resultRows;
      T.assert(3===rows.length).
        assert(6===rows[0]);
    });

    await wtest('exec',{sql: 'delete from t where a>3'});

    await wtest('exec',{
      sql: 'select count(a) from t',
      resultRows: []
    },function(ev){
      ev = ev.result;
      T.assert(1===ev.resultRows.length)
        .assert(2===ev.resultRows[0][0]);
    });

    await wtest('export', function(ev){
      ev = ev.result;
      T.assert('string' === typeof ev.filename)
        .assert(ev.byteArray instanceof Uint8Array)
        .assert(ev.byteArray.length > 1024)
        .assert('application/x-sqlite3' === ev.mimetype);
    });

    /***** close() tests must come last. *****/
    await wtest('close',{},function(ev){
      T.assert('string' === typeof ev.result.filename);
    });

    await wtest('close', (ev)=>{
      T.assert(undefined === ev.result.filename);
    }).finally(()=>logHtml('',"That's all, folks!"));
  }/*runTests2()*/;

  runTests();
})();
