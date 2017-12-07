Function Get-ODBCList
{
    <#
	.SYNOPSIS
	    Get ODBC list.

	.DESCRIPTION
	    The Get-ODBCList cmdlet to Get ODBC connectors list on specific machine.
		
	.PARAMETER ComputerName
	    Specifies a computer name. 
		
	.EXAMPLE
		PS C:\> Get-ODBCList -ComputerName "g1","g2","g3"

		ODBC                                                        ComputerName
		----                                                        ------------
		SQL Server                                                  g1
		Microsoft Access Driver (*.mdb, *.accdb)                    g1
		Microsoft Excel Driver (*.xls, *.xlsx, *.xlsm, *.xlsb)      g1
		Microsoft Access dBASE Driver (*.dbf, *.ndx, *.mdx)         g1
		Microsoft Access Text Driver (*.txt, *.csv)                 g1
		SQL Server Native Client 10.0                               g1
		SQL Native Client                                           g1
		SQL Server                                                  g2
		SQL Server                                                  g3
		
	.NOTES
		Author: Michal Gajda
		Blog  : http://commandlinegeeks.com/
	#> 
	[CmdletBinding(
		SupportsShouldProcess=$True,
		ConfirmImpact="Low"
	)]
	Param
	(
		[parameter(ValueFromPipeline=$true,
			ValueFromPipelineByPropertyName=$true)]		
		[String[]]$ComputerName = $env:COMPUTERNAME
	)

	Begin
	{
		$Key = "SOFTWARE\ODBC\ODBCINST.INI\ODBC Drivers"
		$KeyWow64 = "SOFTWARE\Wow6432Node\ODBC\ODBCINST.INI\ODBC Drivers"
		$Type = [Microsoft.Win32.RegistryHive]::LocalMachine
	} #End Begin
	
	Process
	{
		Foreach($Computer in $ComputerName)
		{
			If(Test-Connection $Computer -Quiet)
			{
				Try
				{
					$regKey = [Microsoft.Win32.RegistryKey]::OpenRemoteBaseKey($Type, $Computer)
					$regSubKey = $regKey.OpenSubKey($Key)
					$regSubKeyWow64 = $regKey.OpenSubKey($KeyWow64)
				}
				Catch
				{
					Write-Error $_.Exception.Message -ErrorAction Stop 
				}
				
				if($regSubKey -and $regSubKeyWow64)
				{
					$Bits = 64
				} #End if $regSubKey -and $regSubKeyWow64
				else
				{
					$Bits = 32
				} #End Else $regSubKey -and $regSubKeyWow64
				
				Foreach($val in $regSubKey.GetValueNames())
				{
					$log = New-Object PSObject -Property @{
						ComputerName = $Computer
						ODBC = $val
						Bits = $Bits
					} #End New-Object PSObject
					
					$log
				} #End ForEach $val in $regSubKey.GetValueNames()
				
				if($Bits -eq 64)
				{
					Foreach($val in $regSubKeyWow64.GetValueNames())
					{
						$log = New-Object PSObject -Property @{
							ComputerName = $Computer
							ODBC = $val	
							Bits = 32
						} #End New-Object PSObject
						
						$log
					} #End ForEach $val in $regSubKeyWow64.GetValueNames()
				} #End If $Bits -eq 64
			} #End If Test-Connection $Computer -Quiet
		} #End Foreach $Computer in $ComputerName
	} #End Process
	
	End{}
} #In The End :)