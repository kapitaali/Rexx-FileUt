/* FileUt: example of writing to a process and having an error
 * Copyright 1999 Patrick TJ McPhee
 * $Header: C:/ptjm/rexx/rexxfile/RCS/errorsort.rex 1.1 1999/03/14 20:26:00 pmcphee Exp $
 */

call rxfuncadd 'fileloadfuncs','rexxfile','fileloadfuncs'
call fileloadfuncs

cmd = 'sort -x'

do 100
  call filelineout cmd,random()
  end

call fileclose(cmd,'in')

do while filechars(cmd)
   say filelinein(cmd)
   end

do while filechars(cmd,'error')
   say 'error:' filelinein(cmd,,,'error')
   end

say 'close:' fileclose(cmd)
