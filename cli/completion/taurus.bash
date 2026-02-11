#!/usr/bin/env bash
# Bash completion for taurus CLI
# Install to: /etc/bash_completion.d/taurus or ~/.bash_completion.d/taurus

_taurus() {
    local cur prev words cword
    _init_completion || return
    
    local commands="parse xpath format version"
    local global_opts="-v --verbose -q --quiet --color --no-color -h --help --version"
    
    # Complete commands
    if [[ $cword -eq 1 ]]; then
        COMPREPLY=($(compgen -W "$commands" -- "$cur"))
        return
    fi
    
    # Complete command-specific options
    local command="${words[1]}"
    case "$command" in
        parse)
            local opts="--format --validate --recover --noout --help"
            if [[ $prev == "--format" ]]; then
                COMPREPLY=($(compgen -W "xml json text" -- "$cur"))
            elif [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "$opts" -- "$cur"))
            else
                # Complete XML files or stdin (-)
                if [[ $cur == "" || $cur == "-" ]]; then
                    COMPREPLY=("-" $(compgen -f -X '!*.xml' -- "$cur"))
                else
                    COMPREPLY=($(compgen -f -X '!*.xml' -- "$cur"))
                fi
            fi
            ;;
        xpath)
            local opts="--format --count --boolean --nsfile --help"
            if [[ $prev == "--format" ]]; then
                COMPREPLY=($(compgen -W "xml json text" -- "$cur"))
            elif [[ $prev == "--nsfile" ]]; then
                COMPREPLY=($(compgen -f -- "$cur"))
            elif [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "$opts" -- "$cur"))
            elif [[ $cword -eq 2 ]]; then
                # First argument: XML file or stdin
                if [[ $cur == "" || $cur == "-" ]]; then
                    COMPREPLY=("-" $(compgen -f -X '!*.xml' -- "$cur"))
                else
                    COMPREPLY=($(compgen -f -X '!*.xml' -- "$cur"))
                fi
            else
                # Second argument: XPath expression (no completion)
                COMPREPLY=()
            fi
            ;;
        format)
            local opts="--format --indent --compact --encode --output -o --help"
            if [[ $prev == "--format" ]]; then
                COMPREPLY=($(compgen -W "xml json text" -- "$cur"))
            elif [[ $prev == "--indent" ]]; then
                COMPREPLY=($(compgen -W "2 4 8" -- "$cur"))
            elif [[ $prev == "--encode" ]]; then
                COMPREPLY=($(compgen -W "UTF-8 UTF-16" -- "$cur"))
            elif [[ $prev == "--output" ]] || [[ $prev == "-o" ]]; then
                COMPREPLY=($(compgen -f -- "$cur"))
            elif [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "$opts" -- "$cur"))
            else
                # Complete XML files or stdin (-)
                if [[ $cur == "" || $cur == "-" ]]; then
                    COMPREPLY=("-" $(compgen -f -X '!*.xml' -- "$cur"))
                else
                    COMPREPLY=($(compgen -f -X '!*.xml' -- "$cur"))
                fi
            fi
            ;;
        version)
            local opts="--short --help"
            if [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "$opts" -- "$cur"))
            fi
            ;;
    esac
}

complete -F _taurus taurus