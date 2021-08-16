[Edyn](https://github.com/xissburg/edyn) library changed for my needs.

Differences of this fork:
- Compability with *entt-3.8.1*

Reminder to myself: never use dynamic linking with this library on MS Windows. The reason is that there is possibility that there will be created 2 separate instances of some template functions and *entt::registry* would use the same pool for different types which will lead to  unpleasant crash.
