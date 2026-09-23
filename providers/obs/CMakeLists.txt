if(NOT STUDIO_REMOTE_ENABLE_PROVIDER_OBS)
  message(FATAL_ERROR "The current production composition requires the OBS provider")
endif()

# The implemented targets remain in the root build and core/obs_adapter until provider-port
# extraction gives a mechanical move an independently reviewed reason.
