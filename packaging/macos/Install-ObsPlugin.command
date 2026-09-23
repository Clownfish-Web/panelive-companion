#!/bin/bash
# Interactive plugin installer; the user plugin location needs no sudo.
package_directory=$(cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
if /bin/bash "$package_directory/Install-ObsPlugin.sh" "$@"; then
  result=0
  printf '\n%s\n' 'Plugin installation completed. You can start OBS now.'
else
  result=$?
  printf '\n%s\n' "Plugin installation failed with code $result."
fi
if [[ -t 0 ]]; then
  read -r -p 'Press Return to close this installer. ' response
fi
exit "$result"
