/* FileUt: example of writing to two processes and reading back the
 * results.
 * Copyright 1999 Patrick TJ McPhee
 * $Header: C:/ptjm/rexx/rexxfile/RCS/twosort.rex 1.1 1999/03/14 20:25:38 pmcphee Exp $
 */
call rxfuncadd 'fileloadfuncs','rexxfile','fileloadfuncs'
call fileloadfuncs

cmd = 'sort -n'

do i = 1 to 100
  stem.i = random()
  end
stem.0 = 100

handle1 = fileopen(cmd, 'pipe')
handle2 = fileopen(cmd, 'pipe')

do i = 1 to 100
  if i // 2 then
    handle = handle1
  else
    handle = handle2

  call filelineout handle, stem.i
  end

call fileclose(handle1,'in')
call fileclose(handle2,'in')

do while filechars(handle1)
   say filelinein(handle1)
   end

do while filechars(handle2)
   say filelinein(handle2)
   end

say 'close:' fileclose(handle1)
say 'close:' fileclose(handle2)
