let g:_executable = 'glitch'
let g:_arguments = ''
let g:_envs = { 'DISPLAY': ':69', 'LOG_LEVEL': '3', 'DEBUG': '1' }
let g:_make = 'make -B'

set makeprg=make
set errorformat=%f:%l:%c:\ %m
packadd termdebug

let g:termdebug_config = {}
let g:termdebug_config['variables_window'] = v:true

nnoremap <leader>x :call LocalRun()<CR>
nnoremap <leader>c :call LocalMake()<CR>
nnoremap <leader>m :call LocalDebugMain()<CR>
nnoremap <leader>l :call LocalDebugLine()<CR>

function! LocalRun() abort
	let envs = join( map(items(g:_envs), { _, kv -> kv[0] . '=' . kv[1] }), ' ')
	execute printf("term env %s ./%s %s", envs, g:_executable, g:_arguments)
endfunction

function! LocalDebugMain() abort
	execute printf('Termdebug %s %s', g:_executable, g:_arguments)

	for [k, v] in items(g:_envs)
		call TermDebugSendCommand(printf('set env %s %s', k, v))
	endfor

	call TermDebugSendCommand('directory ' . getcwd())
	call TermDebugSendCommand('break main')
	call TermDebugSendCommand('run')
endfunction

function! LocalDebugLine() abort
	execute printf('Termdebug %s %s', g:_executable, g:_arguments)

	for [k, v] in items(g:_envs)
		call TermDebugSendCommand(printf('set env %s %s', k, v))
	endfor

	call TermDebugSendCommand('directory ' . getcwd())
	call TermDebugSendCommand(printf('break %s:%d', expand('%:p'), line('.')))
	call TermDebugSendCommand('run')
endfunction

function! LocalMake() abort
	let envs = join( map(items(g:_envs), { _, kv -> kv[0] . '=' . kv[1] }), ' ')
	execute printf('silent !env %s %s', g:_make, envs)

	" Filter non valid errors out of quicklist.
	let qfl = getqflist()
	let filtered = filter(copy(qfl), {_, entry -> entry.valid == 1})
	call setqflist(filtered, 'r')

	redraw!

	if len(filtered) > 0
		execute exists(':CtrlPQuickfix') ? 'CtrlPQuickfix' : 'copen'
	else
		cclose
	endif
endfunction
