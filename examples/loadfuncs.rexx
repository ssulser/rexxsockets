call RxFuncAdd "RSLoadFuncs", "rexxsockets", "RSLoadFuncs"
result = RSLoadFuncs()
if result \= 0 then do
  say "RSLoadFuncs failed rc="result
  exit 1
end
say RSVersion()
call RSUnloadFuncs
return
