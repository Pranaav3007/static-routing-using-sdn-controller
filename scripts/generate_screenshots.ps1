Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$captures = @(
    @{ Input = "README.md"; Output = "screenshots/static-routing/code/README" },
    @{ Input = "REPORT.md"; Output = "screenshots/static-routing/code/REPORT" },
    @{ Input = "controller/routing_policy.py"; Output = "screenshots/static-routing/code/routing_policy" },
    @{ Input = "controller/static_routing_controller.py"; Output = "screenshots/static-routing/code/static_routing_controller" },
    @{ Input = "topology/static_routing_topology.py"; Output = "screenshots/static-routing/code/static_routing_topology" },
    @{ Input = "tests/test_routing_policy.py"; Output = "screenshots/static-routing/code/test_routing_policy" },
    @{ Input = "screenshots/static-routing/logs/test-routing-policy.txt"; Output = "screenshots/static-routing/logs/test-routing-policy" },
    @{ Input = "screenshots/static-routing/logs/compileall.txt"; Output = "screenshots/static-routing/logs/compileall" },
    @{ Input = "screenshots/static-routing/live-demo/PLACEHOLDER.md"; Output = "screenshots/static-routing/live-demo/capture-checklist" }
)

Get-ChildItem "screenshots/static-routing/code" -Filter "*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem "screenshots/static-routing/logs" -Filter "*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem "screenshots/static-routing/live-demo" -Filter "*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

foreach ($capture in $captures) {
    & "$PSScriptRoot/render_text_to_png.ps1" -InputPath $capture.Input -OutputPrefix $capture.Output
}
