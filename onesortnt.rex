/* FileUt: example of writing to a process and reading back the
 * results.
 * Copyright 1999 Patrick TJ McPhee
 * $Header: C:/ptjm/rexx/rexxfile/RCS/onesortnt.rex 1.1 1999/03/16 19:24:10 pmcphee Exp $
 */

call rxfuncadd 'fileloadfuncs','rexxfile','fileloadfuncs'
call fileloadfuncs

cmd = 'sort'

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
