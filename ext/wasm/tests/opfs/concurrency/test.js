(async function(self){
  const btnGo = document.querySelector('#gogogo');

  const logCss = (function(){
    const mapToString = (v)=>{
      switch(typeof v){
          case 'number': case 'string': case 'boolean':
          case 'undefined': case 'bigint':
            return ''+v;
          default: break;
      }
      if( v instanceof Date ) return v.toString();
      if(null===v) return 'null';
      if(v instanceof Error){
        v = {
          message: v.message,
          stack: v.stack,
          errorClass: v.name
        };
      }
      return JSON.stringify(v,undefined,2);
    };
    const normalizeArgs = (args)=>args.map(mapToString);
    const logTarget = document.querySelector('#test-output');
    const logCss = function(cssClass,...args){
      const ln = document.createElement('div');
      if(cssClass){
        for(const c of (Array.isArray(cssClass) ? cssClass : [cssClass])){
          ln.classList.add(c);
        }
      }
      ln.append(document.createTextNode(normalizeArgs(args).join(' ')));
      logTarget.append(ln);
    };
    const cbReverse = document.querySelector('#cb-log-reverse');
    const cbReverseKey = 'tester1:cb-log-reverse';
    const cbReverseIt = ()=>{
      logTarget.classList[cbReverse.checked ? 'add' : 'remove']('reverse');
      localStorage.setItem(cbReverseKey, cbReverse.checked ? 1 : 0);
    };
    cbReverse.addEventListener('change', cbReverseIt, true);
    if(localStorage.getItem(cbReverseKey)){
      cbReverse.checked = !!(+localStorage.getItem(cbReverseKey));
    }
    cbReverseIt();
    return logCss;
  })();
  const stdout = (...args)=>logCss('',...args);
  const stderr = (...args)=>logCss('error',...args);

  const wait = async (ms)=>{
    return new Promise((resolve)=>setTimeout(resolve,ms));
  };

  const urlArgsJs = new URL(document.currentScript.src).searchParams;
  const urlArgsHtml = new URL(self.location.href).searchParams;
  const options = Object.create(null);
  options.sqlite3Dir = urlArgsJs.get('sqlite3.dir');
  options.workerCount = (
    urlArgsHtml.has('workers') ? +urlArgsHtml.get('workers') : 0
  ) || 3;
  options.opfsVerbose = (
    urlArgsHtml.has('verbose') ? +urlArgsHtml.get('verbose') : 1
  ) || 0;
  options.interval = (
    urlArgsHtml.has('interval') ? +urlArgsHtml.get('interval') : 1000
  ) || 1000;
  options.iterations = (
    urlArgsHtml.has('iterations') ? +urlArgsHtml.get('iterations') : 10
  ) || 10;
  options.unlockAsap = (
    urlArgsHtml.has('unlock-asap') ? +urlArgsHtml.get('unlock-asap') : 0
  ) || 0;
  options.vfs = urlArgsHtml.get('vfs') || 'opfs';
  options.noUnlink = !!urlArgsHtml.has('no-unlink');
  const workers = [];
  workers.post = (type,...args)=>{
    for(const w of workers) w.postMessage({type, payload:args});
  };
  workers.counts = {loaded: 0, passed: 0, failed: 0};
  let timeStart;
  const calcTime = (endDate)=>{
    return endDate.getTime() - timeStart?.getTime?.();
  };
  const checkFinished = function(){
    if(workers.counts.passed + workers.counts.failed !== workers.length){
      return;
    }
    if(workers.counts.failed>0){
      logCss('tests-fail',"Finished with",workers.counts.failed,"failure(s) in",
             calcTime(new Date()),"ms");
    }else{
      logCss('tests-pass',"All",workers.length,"workers finished in",
             calcTime(new Date()),"ms");
    }
    logCss("Reload page to run the test again.");
  };

  workers.onmessage = function(msg){
    msg = msg.data;
    const prefix = 'Worker #'+msg.worker+':';
    switch(msg.type){
      case 'loaded':
        stdout(prefix,"loaded");
        if(++workers.counts.loaded === workers.length){
          timeStart = new Date();
          stdout(timeStart,"All",workers.length,"workers loaded. Telling them to run...");
          workers.post('run');
        }
        break;
      case 'stdout': stdout(prefix,...msg.payload); break;
      case 'stderr': stderr(prefix,...msg.payload); break;
      case 'error': stderr(prefix,"ERROR:",...msg.payload); break;
      case 'finished': {
        ++workers.counts.passed;
        const timeEnd = new Date();
        logCss('tests-pass',prefix,timeEnd,"("+calcTime(timeEnd)+"ms) ", ...msg.payload);
        checkFinished();
        break;
      }
      case 'failed': {
        ++workers.counts.failed;
        const timeEnd = new Date();
        logCss('tests-fail',prefix,timeEnd,"("+calcTime(timeEnd)+"ms) FAILED:",...msg.payload);
        checkFinished();
        break;
      }
      default: logCss('error',"Unhandled message type:",msg); break;
    }
  };

  /* Set up links to launch this tool with various combinations of
     flags... */
  const eTestLinks = document.querySelector('#testlinks');
  const optArgs = function(obj){
    const li = [];
    for(const k of [
      'interval', 'iterations', 'unlock-asap',
      'opfsVerbose', 'vfs', 'workers'
    ]){
      if( obj.hasOwnProperty(k) ) li.push(k+'='+obj[k]);
    }
    return li.join('&');
  };
  for(const opt of [
    {interval: 1000, workers: 5, iterations: 30},
    {interval: 500, workers: 5, iterations: 30},
    {interval: 250, workers: 3, iterations: 30},
    {interval: 600, workers: 5, iterations: 100},
    {interval: 500, workers: 1, iterations: 5, vfs: 'opfs-wl'},
    {interval: 500, workers: 2, iterations: 10, vfs: 'opfs-wl'},
    {interval: 500, workers: 5, iterations: 20, vfs: 'opfs-wl'},
  ]){
    const li = document.createElement('li');
    eTestLinks.appendChild(li);
    const a = document.createElement('a');
    li.appendChild(a);
    const args = optArgs(opt);
    a.setAttribute('href', '?'+args);
    a.innerText = args;
  }

  btnGo.addEventListener('click', ()=>{
    btnGo.remove();
    stdout("Launching",options.workerCount,"workers. Options:",options);
    workers.uri = (
      'worker.js?'
        + 'sqlite3.dir='+options.sqlite3Dir
        + '&vfs='+options.vfs
        + '&interval='+options.interval
        + '&iterations='+options.iterations
        + '&opfs-verbose='+options.opfsVerbose
        + '&opfs-unlock-asap='+options.unlockAsap
    );
    for(let i = 0; i < options.workerCount; ++i){
      stdout("Launching worker...", i, );
      workers.push(new Worker(
        workers.uri+'&workerId='+(i+1)+(
          (i || options.noUnlink) ? '' : '&unlink-db'
        )
      ));
    }
    // Have to delay onmessage assignment until after the loop
    // to avoid that early workers get an undue head start.
    workers.forEach((w)=>w.onmessage = workers.onmessage);
  });
})(globalThis);
