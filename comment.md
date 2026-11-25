Consistant code convention. Code must follow a consistant standard. This includes file names, function names, variable name, structure names, indentation, brace placement, etc. (e.g. The grader should not wonder why two code entities of similar nature be named differently).	

comment:
The project generally follows consistent C naming, file naming, and formatting conventions across its gameplay modules. Some older comments and formatting are less consistent, so there is still room to polish the codebase.

"Organized file structure in Solution Explorer
Code must be categorized into different files in a logical and meaningful way to aid code navigation. Avoid vague categories which can include all code like ""utils"" or ""helpers"". You may make use of filters or prefixing file names. The grader should not be troubled when searching for a specific piece of code (e.g. ""where is collision?"")."	

comment:
The code is divided into files by responsibility, such as player, entity, map extraction, menus, pause handling, and quizzes. This makes features easier to locate than keeping all logic in one source file.

"Good Readability
Code must be readable and not messy. Naming should be sensible. Comments should explain code that are are hard to understand at first glance. Global variables must not be littered all over the codebase. Literals must have a derivable meaning. Chunks of code should not be copy pasted with minor modifications."	

comment:
Names and comments explain most of the game logic, and shared behavior is separated into modules. Some shared state is global, and a few implementation details could be documented or simplified further.

"Codebase is made up of identifiable systems 
A system is an identifiable group of related code that represents a feature or behaviour that is seperate from the rest of the codebase (e.g. code for particle system is seperate from UI system, unless UI needs particles). These systems should ideally be scalable."	

comment:
The game has distinct systems for player movement, entities, map loading, UI, audio, quizzes, and visual effects. These modules provide a foundation for extending individual features without placing all logic in one system.

"Product has meaningful and robust debugging features
These are features that lets you inspect and interact with your program while it runs. They should genuinely help you find and fix problems, not just show surface-level information. Some ideas include: 
> Profiling and visualizing performance of different critical sections of code
> Inspecting and editing entity variables as the game is running
> Ability to forward and rewind frames while viewing all collision hitboxes
> A drop down in-game console where you can give commands to do various actions as the game is running"	

comment:
The game includes a debug mode with hitbox visualization, pausing, single-frame stepping, and frame rewind using saved snapshots. It helps inspect gameplay and collisions, although it does not yet include profiling or live editing of entity variables.

"Product has meaningful and robust content creation features
Product uses visual content creation tools that allow a designer to fully customize every gameplay or UI aspect of their game and view their changes without restarting the game. For gameplay, this includes level data, player data, enemy data, etc. For UI, this includes positions, text, anchoring, size, etc. Examples of good content creation tool are:
> In-game visual editor that allows a designer to easily design, load and save their levels or UI elements while the game is running.
> External visual editor with hot reloading, which allows designers to modify content outside the game, with changes automatically reflected in real time while the game is running."	

comment:
Levels are authored visually in Tiled and loaded from exported map data, so terrain and objects can be placed without changing game code. Changes currently require exporting the map and restarting the game; there is no in-game editor or hot reloading.

Product Vision fulfilment. Instructors are convinced that the product fulfill the SCQA stated in the Product Information section.	

comment:
The game addresses the goal of making quizzes more engaging by placing multiplication questions at checkpoints and rewarding correct answers with a choice of gameplay buff.

Cohesive experience. Entire product comes together as one solid experience. Users must not feel like they are using mulitple different products at all times. In general, the product is ready to be launched and expects positive feedback from users.

comment:
Platforming, quizzes, checkpoints, rewards, menus, audio, and pixel-art visuals are combined in one game flow. The consistent theme and shared interface help the features feel connected.

Completeness. Product must have a start, middle and end with a gameplay loop to reattempt the game. It must fulfill the ""REQUIREMENT: Content"" section in the REQUIREMENTS sheet.

comment:
The game has a start menu, a playable level, checkpoints, and an end gate. Players can restart or replay the level, completing a basic start-to-finish gameplay loop.

Good Feedback Quality. All interactions are responsive and satisfying. This includes visual and audio feedback. For example, buttons must minimally have on-hover, on-hold, and on-up feedback.

comment:
The game gives feedback through button hover states, animation, score popups, particle effects, and sound effects for key actions. Some buttons do not have distinct pressed or released visuals yet, so that feedback could be improved.

"Clear goals. User must not be confused at any interaction or lost at any scene or screens. You may assume that the user understand basic computer interactions such as clicking on buttons, pressing keys and dragging and dropping. Do not assume that the user play lots of games. All explainations must be within the product; explaining how the game works in this document does not count."

commnet:
The menu and pause screens identify their actions, and the instructions screen explains the core keyboard controls. More in-game guidance could help new players understand checkpoints, quizzes, and buffs without needing an external explanation.

Consistent Controls. All interactions and controls should feel consistant and cohesive in general. For example, exiting the credits screen should use similar controls as exiting from other menus. There can be leeway given between the implementation of menu controls and gameplay controls.

comment:
Menus use familiar click and back behavior, while gameplay uses keyboard controls. Escape backs out of pause-menu pages, and the menu screens provide clear ways to return.

Consistant layout, choice of colors and style. Some leeway may be given if product uses a mix of free assets and self-created assets, but some effort must be made to ensure consistancy. For example, if pixel style is chosen, pixel style must be maintained across the product. Assets must not look like placeholders.	

commet:
The game maintains a pixel-art style and uses a recurring cream, gold, and red palette for its interface. The matching sprites and panels help make gameplay and menus feel visually related.

Good alignment of visuals. Visual elements must be arranged in a neat fashion without breaking alignment carelessly. This includes centering of text, arrangement of UI elements, etc.	

comment:
Menus, quiz panels, and buff choices use centered layouts with aligned text and buttons, helping players scan the available choices.

Non-interfering color palette. Colors chosen must be not interfere with any part of user interaction. For example, white text on bright yellow background makes it unreadable.	

comment:
Text is generally displayed against contrasting panels or backgrounds, and hover highlights help distinguish interactive controls. This supports readability across the menus and quiz.

Smooth view transitions. Switching between views must not be jarring or sudden. Ideally, all views should have a transition. For example, going from main menu to any other screens should have fade-in and fade-out transitions.	

comment:
Menu and gameplay state changes use an animated transition image with timed fade phases, making those changes feel less abrupt.

Good use of audio. Has background music and sound effects for all interactions without being frustrating to the user.	

comment:
The game has separate menu and gameplay music, plus effects for key actions such as clicking, jumping, collecting items, and taking damage. Music and effects have separate volume controls and can be muted.