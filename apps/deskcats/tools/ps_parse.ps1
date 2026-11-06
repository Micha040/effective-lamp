param([string[]]$Paths)
foreach ($p in $Paths) {
    $tokens = $null; $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($p, [ref]$tokens, [ref]$errors)
    # Syntax, die Windows PowerShell 5.1 nicht kennt
    $ps7 = @($tokens | Where-Object { $_.Kind -in 'QuestionQuestion', 'QuestionQuestionEquals', 'QuestionDot', 'QuestionLBracket', 'AndAnd', 'OrOr' -or ($_.Kind -eq 'QuestionMark') })
    $funcs = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)).Count
    "{0}: parse errors={1}, PS7-only tokens={2}, functions={3}" -f $p, $errors.Count, $ps7.Count, $funcs
    foreach ($e in $errors) { "  ERR line $($e.Extent.StartLineNumber): $($e.Message)" }
    foreach ($t in $ps7) { "  PS7 token line $($t.Extent.StartLineNumber): $($t.Text)" }
}
