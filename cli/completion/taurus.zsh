#compdef taurus
# Zsh completion for taurus CLI
# Install to: /usr/local/share/zsh/site-functions/_taurus

_taurus() {
    local -a commands
    commands=(
        'parse:Parse and validate XML documents'
        'xpath:Execute XPath queries'
        'format:Pretty-print XML documents'
        'version:Show version information'
    )
    
    local -a global_opts
    global_opts=(
        '(-v --verbose)'{-v,--verbose}'[Increase verbosity]'
        '(-q --quiet)'{-q,--quiet}'[Suppress warnings]'
        '--color[Enable colored output]'
        '--no-color[Disable colored output]'
        '(-h --help)'{-h,--help}'[Show help]'
        '--version[Show version]'
    )
    
    _arguments -C \
        $global_opts \
        '1: :->command' \
        '*:: :->args'
    
    case $state in
        command)
            _describe 'command' commands
            ;;
        args)
            case $words[1] in
                parse)
                    _arguments \
                        '--format[Output format]:format:(xml json text)' \
                        '--validate[Enable validation]' \
                        '--recover[Enable error recovery]' \
                        '--noout[Suppress output]' \
                        '(-h --help)'{-h,--help}'[Show help]' \
                        '*:file:_files -g "*.xml" -g "-"'
                    ;;
                xpath)
                    _arguments \
                        '--format[Output format]:format:(xml json text)' \
                        '--count[Output count only]' \
                        '--boolean[Output boolean only]' \
                        '--nsfile[Namespace bindings file]:file:_files' \
                        '(-h --help)'{-h,--help}'[Show help]' \
                        '1:file:_files -g "*.xml" -g "-"' \
                        '2:expression:'
                    ;;
                format)
                    _arguments \
                        '--format[Output format]:format:(xml json text)' \
                        '--indent[Indentation size]:size:(2 4 8)' \
                        '--compact[Remove whitespace]' \
                        '--encode[Output encoding]:encoding:(UTF-8 UTF-16)' \
                        '(-o --output)'{-o,--output}'[Output file]:file:_files' \
                        '(-h --help)'{-h,--help}'[Show help]' \
                        '*:file:_files -g "*.xml" -g "-"'
                    ;;
                version)
                    _arguments \
                        '--short[Short version]' \
                        '(-h --help)'{-h,--help}'[Show help]'
                    ;;
            esac
            ;;
    esac
}

_taurus "$@"