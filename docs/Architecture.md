# Video.Server.GStreamer

`PiSubmarine.Video.Server.GStreamer` implements the live camera server using GStreamer.

## Responsibility

This module owns:

- lease-backed video subscription management
- operator-driven enable or disable state
- automatic camera start when there are valid subscribers and video is enabled
- automatic camera stop when there are no valid subscribers
- GStreamer pipeline lifecycle and endpoint updates

It does not own:

- lease issuance
- gRPC transport
- operator command transport

## Public Boundaries

`Controller` directly implements:

- `Control.Video.Api::IController`
- `Video.Subscription.Api::IService`
- `Time::ITickable`

This keeps the module concrete while reusing existing cross-module abstractions.

## Source Selection

Camera source selection is configured by composition root.

The first version supports:

- `AutoDetectSource`
- `ElementSource`

`AutoDetectSource` prefers known GStreamer source elements in a configurable order.
`ElementSource` is the escape hatch for exact source descriptions, which is useful for
multi-camera setups and platform-specific device selection.
