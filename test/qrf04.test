# 2025-11-23
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#***********************************************************************
#
# Test cases for the Query Result Formatter (QRF), and especially
# the bSplitColumn feature.
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix qrf01

# The expected output from test 1.1.  The "do_test" procedure normally
# ignores differences in whitespace, but whitespace is important for
# this test, so we have to do the comparison ourselves.
#
set expected {
<----     22     ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----      23     ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----      24      ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----       25      ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----       26       ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----        27       ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----        28        ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----         29        ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----         30         ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----          31         ---->
alice
bob
cinderella-cinderella
daniel
emma
fred
gertrude
harold
ingrid
jake
lisa
mike
nina
octavian
paula
quintus
rita
sam
tammy
ulysses
violet
william
xanthippe
yates
zoe
<----          32          ---->
alice                  octavian
bob                    paula
cinderella-cinderella  quintus
daniel                 rita
emma                   sam
fred                   tammy
gertrude               ulysses
harold                 violet
ingrid                 william
jake                   xanthippe
lisa                   yates
mike                   zoe
nina
<----           33          ---->
alice                   octavian
bob                     paula
cinderella-cinderella   quintus
daniel                  rita
emma                    sam
fred                    tammy
gertrude                ulysses
harold                  violet
ingrid                  william
jake                    xanthippe
lisa                    yates
mike                    zoe
nina
<----           34           ---->
alice                    octavian
bob                      paula
cinderella-cinderella    quintus
daniel                   rita
emma                     sam
fred                     tammy
gertrude                 ulysses
harold                   violet
ingrid                   william
jake                     xanthippe
lisa                     yates
mike                     zoe
nina
<----            35           ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----            36            ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----             37            ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----             38             ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----              39             ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----              40              ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----               41              ---->
alice                     octavian
bob                       paula
cinderella-cinderella     quintus
daniel                    rita
emma                      sam
fred                      tammy
gertrude                  ulysses
harold                    violet
ingrid                    william
jake                      xanthippe
lisa                      yates
mike                      zoe
nina
<----               42               ---->
alice                  jake      tammy
bob                    lisa      ulysses
cinderella-cinderella  mike      violet
daniel                 nina      william
emma                   octavian  xanthippe
fred                   paula     yates
gertrude               quintus   zoe
harold                 rita
ingrid                 sam
<----                43               ---->
alice                  jake      tammy
bob                    lisa      ulysses
cinderella-cinderella  mike      violet
daniel                 nina      william
emma                   octavian  xanthippe
fred                   paula     yates
gertrude               quintus   zoe
harold                 rita
ingrid                 sam
<----                44                ---->
alice                   jake       tammy
bob                     lisa       ulysses
cinderella-cinderella   mike       violet
daniel                  nina       william
emma                    octavian   xanthippe
fred                    paula      yates
gertrude                quintus    zoe
harold                  rita
ingrid                  sam
<----                 45                ---->
alice                   jake       tammy
bob                     lisa       ulysses
cinderella-cinderella   mike       violet
daniel                  nina       william
emma                    octavian   xanthippe
fred                    paula      yates
gertrude                quintus    zoe
harold                  rita
ingrid                  sam
<----                 46                 ---->
alice                    jake        tammy
bob                      lisa        ulysses
cinderella-cinderella    mike        violet
daniel                   nina        william
emma                     octavian    xanthippe
fred                     paula       yates
gertrude                 quintus     zoe
harold                   rita
ingrid                   sam
<----                  47                 ---->
alice                    jake        tammy
bob                      lisa        ulysses
cinderella-cinderella    mike        violet
daniel                   nina        william
emma                     octavian    xanthippe
fred                     paula       yates
gertrude                 quintus     zoe
harold                   rita
ingrid                   sam
<----                  48                  ---->
alice                     jake         tammy
bob                       lisa         ulysses
cinderella-cinderella     mike         violet
daniel                    nina         william
emma                      octavian     xanthippe
fred                      paula        yates
gertrude                  quintus      zoe
harold                    rita
ingrid                    sam
<----                   49                  ---->
alice                     jake         tammy
bob                       lisa         ulysses
cinderella-cinderella     mike         violet
daniel                    nina         william
emma                      octavian     xanthippe
fred                      paula        yates
gertrude                  quintus      zoe
harold                    rita
ingrid                    sam
<----                   50                   ---->
alice                     jake         tammy
bob                       lisa         ulysses
cinderella-cinderella     mike         violet
daniel                    nina         william
emma                      octavian     xanthippe
fred                      paula        yates
gertrude                  quintus      zoe
harold                    rita
ingrid                    sam
<----                    51                   ---->
alice                  harold    paula    william
bob                    ingrid    quintus  xanthippe
cinderella-cinderella  jake      rita     yates
daniel                 lisa      sam      zoe
emma                   mike      tammy
fred                   nina      ulysses
gertrude               octavian  violet
<----                    52                    ---->
alice                  harold    paula    william
bob                    ingrid    quintus  xanthippe
cinderella-cinderella  jake      rita     yates
daniel                 lisa      sam      zoe
emma                   mike      tammy
fred                   nina      ulysses
gertrude               octavian  violet
<----                     53                    ---->
alice                  harold    paula    william
bob                    ingrid    quintus  xanthippe
cinderella-cinderella  jake      rita     yates
daniel                 lisa      sam      zoe
emma                   mike      tammy
fred                   nina      ulysses
gertrude               octavian  violet
<----                     54                     ---->
alice                   harold     paula     william
bob                     ingrid     quintus   xanthippe
cinderella-cinderella   jake       rita      yates
daniel                  lisa       sam       zoe
emma                    mike       tammy
fred                    nina       ulysses
gertrude                octavian   violet
<----                      55                     ---->
alice                   harold     paula     william
bob                     ingrid     quintus   xanthippe
cinderella-cinderella   jake       rita      yates
daniel                  lisa       sam       zoe
emma                    mike       tammy
fred                    nina       ulysses
gertrude                octavian   violet
<----                      56                      ---->
alice                   harold     paula     william
bob                     ingrid     quintus   xanthippe
cinderella-cinderella   jake       rita      yates
daniel                  lisa       sam       zoe
emma                    mike       tammy
fred                    nina       ulysses
gertrude                octavian   violet
<----                       57                      ---->
alice                    harold      paula      william
bob                      ingrid      quintus    xanthippe
cinderella-cinderella    jake        rita       yates
daniel                   lisa        sam        zoe
emma                     mike        tammy
fred                     nina        ulysses
gertrude                 octavian    violet
<----                       58                       ---->
alice                    harold      paula      william
bob                      ingrid      quintus    xanthippe
cinderella-cinderella    jake        rita       yates
daniel                   lisa        sam        zoe
emma                     mike        tammy
fred                     nina        ulysses
gertrude                 octavian    violet
<----                        59                       ---->
alice                    harold      paula      william
bob                      ingrid      quintus    xanthippe
cinderella-cinderella    jake        rita       yates
daniel                   lisa        sam        zoe
emma                     mike        tammy
fred                     nina        ulysses
gertrude                 octavian    violet
<----                        60                        ---->
alice                     harold       paula       william
bob                       ingrid       quintus     xanthippe
cinderella-cinderella     jake         rita        yates
daniel                    lisa         sam         zoe
emma                      mike         tammy
fred                      nina         ulysses
gertrude                  octavian     violet
<----                         61                        ---->
alice                  fred      lisa      quintus  violet
bob                    gertrude  mike      rita     william
cinderella-cinderella  harold    nina      sam      xanthippe
daniel                 ingrid    octavian  tammy    yates
emma                   jake      paula     ulysses  zoe
<----                         62                         ---->
alice                  fred      lisa      quintus  violet
bob                    gertrude  mike      rita     william
cinderella-cinderella  harold    nina      sam      xanthippe
daniel                 ingrid    octavian  tammy    yates
emma                   jake      paula     ulysses  zoe
<----                          63                         ---->
alice                  fred      lisa      quintus  violet
bob                    gertrude  mike      rita     william
cinderella-cinderella  harold    nina      sam      xanthippe
daniel                 ingrid    octavian  tammy    yates
emma                   jake      paula     ulysses  zoe
<----                          64                          ---->
alice                  fred      lisa      quintus  violet
bob                    gertrude  mike      rita     william
cinderella-cinderella  harold    nina      sam      xanthippe
daniel                 ingrid    octavian  tammy    yates
emma                   jake      paula     ulysses  zoe
<----                           65                          ---->
alice                   fred       lisa       quintus   violet
bob                     gertrude   mike       rita      william
cinderella-cinderella   harold     nina       sam       xanthippe
daniel                  ingrid     octavian   tammy     yates
emma                    jake       paula      ulysses   zoe
<----                           66                           ---->
alice                   fred       lisa       quintus   violet
bob                     gertrude   mike       rita      william
cinderella-cinderella   harold     nina       sam       xanthippe
daniel                  ingrid     octavian   tammy     yates
emma                    jake       paula      ulysses   zoe
<----                            67                           ---->
alice                   fred       lisa       quintus   violet
bob                     gertrude   mike       rita      william
cinderella-cinderella   harold     nina       sam       xanthippe
daniel                  ingrid     octavian   tammy     yates
emma                    jake       paula      ulysses   zoe
<----                            68                            ---->
alice                   fred       lisa       quintus   violet
bob                     gertrude   mike       rita      william
cinderella-cinderella   harold     nina       sam       xanthippe
daniel                  ingrid     octavian   tammy     yates
emma                    jake       paula      ulysses   zoe
<----                             69                            ---->
alice                    fred        lisa        quintus    violet
bob                      gertrude    mike        rita       william
cinderella-cinderella    harold      nina        sam        xanthippe
daniel                   ingrid      octavian    tammy      yates
emma                     jake        paula       ulysses    zoe
<----                             70                             ---->
alice                    fred        lisa        quintus    violet
bob                      gertrude    mike        rita       william
cinderella-cinderella    harold      nina        sam        xanthippe
daniel                   ingrid      octavian    tammy      yates
emma                     jake        paula       ulysses    zoe
<----                              71                             ---->
alice                    fred        lisa        quintus    violet
bob                      gertrude    mike        rita       william
cinderella-cinderella    harold      nina        sam        xanthippe
daniel                   ingrid      octavian    tammy      yates
emma                     jake        paula       ulysses    zoe
<----                              72                              ---->
alice                    fred        lisa        quintus    violet
bob                      gertrude    mike        rita       william
cinderella-cinderella    harold      nina        sam        xanthippe
daniel                   ingrid      octavian    tammy      yates
emma                     jake        paula       ulysses    zoe
<----                               73                              ---->
alice                     fred         lisa         quintus     violet
bob                       gertrude     mike         rita        william
cinderella-cinderella     harold       nina         sam         xanthippe
daniel                    ingrid       octavian     tammy       yates
emma                      jake         paula        ulysses     zoe
<----                               74                               ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                75                               ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                76                                ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                 77                                ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                 78                                 ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                  79                                 ---->
alice                  emma      ingrid  nina      rita     violet     zoe
bob                    fred      jake    octavian  sam      william
cinderella-cinderella  gertrude  lisa    paula     tammy    xanthippe
daniel                 harold    mike    quintus   ulysses  yates
<----                                  80                                  ---->
alice                   emma       ingrid   nina       rita      violet      zoe
bob                     fred       jake     octavian   sam       william
cinderella-cinderella   gertrude   lisa     paula      tammy     xanthippe
daniel                  harold     mike     quintus    ulysses   yates
}

do_test 1.0 {
  db eval {
    CREATE TABLE t1(x);
    INSERT INTO t1(x) VALUES
      ('alice'),
      ('bob'),
      ('cinderella-cinderella'),
      ('daniel'),
      ('emma'),
      ('fred'),
      ('gertrude'),
      ('harold'),
      ('ingrid'),
      ('jake'),
      ('lisa'),
      ('mike'),
      ('nina'),
      ('octavian'),
      ('paula'),
      ('quintus'),
      ('rita'),
      ('sam'),
      ('tammy'),
      ('ulysses'),
      ('violet'),
      ('william'),
      ('xanthippe'),
      ('yates'),
      ('zoe');
  }
  set res \n
  for {set i 22} {$i<=80} {incr i} {
    set sp [expr {$i-13}]
    append res [format "<----%*s%3d%*s---->\n" \
                 [expr {$sp/2}] {} $i [expr {$sp-$sp/2}] {}]
    append res [db format -style column -title off \
                   -screenwidth $i -splitcolumn on \
                  {SELECT x FROM t1 ORDER BY x ASC}]
  }
  expr {$res eq $::expected}
} {1}
