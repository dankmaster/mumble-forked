Set-StrictMode -Version Latest

function Get-QmlVisualMatrixShardCases {
	param(
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Cases,
		[Parameter(Mandatory = $true)][ValidateRange(0, [int]::MaxValue)][int]$ShardIndex,
		[Parameter(Mandatory = $true)][ValidateRange(1, [int]::MaxValue)][int]$ShardCount
	)

	if ($ShardIndex -ge $ShardCount) {
		throw "Visual matrix shard index $ShardIndex must be smaller than shard count $ShardCount."
	}
	if ($Cases.Count -eq 0) {
		throw "Visual matrix contains no cases."
	}
	if ($ShardCount -gt $Cases.Count) {
		throw "Visual matrix shard count $ShardCount exceeds the $($Cases.Count) available cases."
	}

	$selected = [Collections.Generic.List[object]]::new()
	for ($caseIndex = $ShardIndex; $caseIndex -lt $Cases.Count; $caseIndex += $ShardCount) {
		$selected.Add($Cases[$caseIndex])
	}
	return $selected.ToArray()
}

function Get-QmlVisualAutomationPort {
	param(
		[Parameter(Mandatory = $true)][ValidateRange(1024, 65535)][int]$BasePort,
		[Parameter(Mandatory = $true)][ValidateRange(0, [int]::MaxValue)][int]$ShardIndex,
		[Parameter(Mandatory = $true)][ValidateRange(1, [int]::MaxValue)][int]$SourceDprCount,
		[Parameter(Mandatory = $true)][ValidateRange(0, [int]::MaxValue)][int]$SourceDprIndex
	)

	if ($SourceDprIndex -ge $SourceDprCount) {
		throw "Visual matrix DPR index $SourceDprIndex must be smaller than DPR count $SourceDprCount."
	}
	$offset = ([long]$ShardIndex * [long]$SourceDprCount) + [long]$SourceDprIndex
	$port = [long]$BasePort + $offset
	if ($port -gt 65535) {
		throw "Visual matrix automation port range exceeds 65535 (base $BasePort, offset $offset)."
	}
	return [int]$port
}

Export-ModuleMember -Function Get-QmlVisualMatrixShardCases, Get-QmlVisualAutomationPort
