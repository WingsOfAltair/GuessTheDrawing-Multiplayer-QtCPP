# Copilot Instructions

## Project Guidelines
- When the user requests a concrete code change, proceed directly and keep progress narration brief; avoid repetitive status chatter.
- For country flags in this app, provide actual graphical flag icons rather than Unicode regional-indicator characters; show both the flag icon and country code wherever user/player countries are displayed, including room player lists.
- When adding countries from the country-flags directory, reconcile every app country-code list and validation surface, especially main_window.cpp and AccountStore, rather than assuming updating the selector alone covers all relevant places.

## Layer Management
- For drawing layers, preserve the user's intended behavior: Move up places the selected stroke underneath layers below it in the list, making those strokes more visible; Move down makes the selected layer's strokes more visible/on top. Do not reverse the button semantics based on a generic higher-order-is-on-top assumption.