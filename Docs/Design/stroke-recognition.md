# Stroke recognition

How a game tells which shape a player drew by hand, and why it works this way. Built:
`PointCloudRecognizer` (`Engine/Source/Runtime/Core`), its template text format, and
`NyxPointCloudRecognizerTests`.

## The recogniser

- **A point-cloud recogniser of the $P family** (Vatavu et al., *Gestures as Point Clouds: A $P
  Recognizer for User Interface Prototypes*, 2012). The strokes become a cloud of
  points and are matched against templates, which are clouds too. A cloud has no order, so a shape
  is the same shape whatever order and direction its strokes were drawn in, and however many
  strokes it took: a triangle in one stroke or three, an arrow's head drawn before its shaft.
- **Normalised as $P does:** resampled to `PointCount` points (32) spread evenly along the strokes,
  the gaps between strokes left out; scaled alike in x and y so that their box's longer side is 1;
  moved so that their centroid is at the origin (`MakePointCloud()`).
- **Matched as $P does:** a greedy match, each point of one cloud taking the nearest point of the
  other not taken yet, from sqrt(n) starting points, in both directions; the first point matched
  counts fully and the last 1/n. The distance is the least weighted sum over them, divided by the
  sum of the weights: the weighted mean distance between matched points, in units of the unit box.
- **Templates are named clouds, several per name** (`AddTemplate()`): a few examples of each shape,
  drawn by different hands, match a new hand better than one. `Recognize()` gives the closest
  template (`Best`), the closest of another name (`RunnerUp`), each with its distance, a `Score`
  (1 at distance 0, down to 0 at `MaxScoredDistance`, 0.25) and the template's index.
- **Close enough is the caller's to say.** The closest template is given whatever its score, so a
  game can say *not recognised: closest is Circle (61%)* below its own threshold. In the tests,
  shapes drawn again with a shaky hand score 0.85 to 0.96, their runners-up 0.44 to 0.6, scribbles
  0.28 to 0.70; 0.75 tells them apart.

## Turns

- **Rotation insensitive, the default:** a shape turned is the same shape. $P itself matches as
  drawn, so every template is tried at 8 turns 45 degrees apart, then at half, a quarter and an
  eighth of that either side of the best of them (to within about 3 degrees): 14 matches a
  template. The strokes are scaled into the unit box after each turn, so every match is $P's at that
  turn. The match says where the template's +Y lay in the strokes (`PointCloudMatch::Up`): which way
  an arrow drawn at any angle points. For shapes that look alike turned (a circle, an equilateral
  triangle a third of a turn on) it is one of the turns that fit.
- **Rotation sensitive** (`bRotationSensitive`): an arrow to the right and one up are two shapes.
  The strokes are matched as drawn, with the up given to `Recognize()` turned onto the templates'
  +Y, e.g. the view's up laid onto the plane the player draws on, so that the player draws as they
  see it. One match a template.
- **No trigonometry of a C library:** turns are made from constants (the cosines and sines of 45,
  22.5, 11.25 and 5.625 degrees) by multiplying, and the up given becomes a turn by normalising it, so
  a recognition gives the same result to the bit on any machine running the same build (a library's
  `cos` and `sin` can differ in their last bit between processors). Ties go to the template added
  first. A host in co-op recognising the same points gets the same answer.

## Cost

- **Every distance once:** a match of a cloud against a template computes the n x n squared
  distances, both ways round, in loops the compiler runs eight columns at a time, and each point's
  nearest in the other cloud. The square roots are taken only of the distances a match takes.
- **Bounded early:** each point's distance in any match is at least its nearest, so their weighted
  sum is a lower bound for a starting point, and a starting point (or a template's turn, or a
  template) whose bound can't beat the result so far isn't matched at all. A match stops as soon as
  its sum, with the bound of the points left, can't. The bounds are taken a thousandth smaller,
  so that rounding never makes one more than the sum it bounds. Rotation insensitive, a template's
  8 turns are matched from the least bound up, so the best turn usually comes first and the rest
  stop at once. What the result so far is: for a template of the best name, the best; for any
  other, the runner-up, so both stay exact.
- **Numbers** (`NyxPointCloudRecognizerTests`, 50 templates of 10 shapes, 32 points, each of nine
  drawings' fastest of 20 recognitions, the median of the drawings and the slowest; Core i7-12700K,
  other programs running): rotation sensitive **0.12 ms** (slowest 0.2 ms) in Release, 1.3 ms in
  Debug; rotation insensitive **0.55 ms** (slowest drawing, a circle, which fits every turn: 1.05
  ms) in Release, 14 ms in Debug. Recognising happens once a drawing ends, on the player's side,
  not in a step. The test fails in Release if the median is 1 ms or more.
- If templates grow into the hundreds, $Q (Vatavu et al., 2018) is the faster
  sibling with the same interface: a look-up table per template for the lower bounds.

## The text format

A game ships its templates as text (`ParseStrokeTemplates()`, `LoadStrokeTemplates()`), and can
write the ones a player records (`WriteStrokeTemplates()`):

```
# Comments and empty lines are skipped
template Arrow
stroke 0 0  1 0
stroke 0.8 0.2  1 0  0.8 -0.2
template Arrow
stroke 1 0  0 0
stroke 0.8 -0.2  1 0  0.8 0.2
```

- `template` starts one template, named by the rest of its line (spaces allowed); each `stroke`
  line below it is one of its strokes, its points as x y pairs in drawing order, +Y up, in any
  units. A name may have several templates.
- Numbers are written in the shortest form that reads back to the same float, so a template written
  and read again matches exactly as before.
- A problem fails the whole text with its line: a stroke before any template, an odd count of
  numbers, something that isn't a number, a template without strokes.
- The file's name and where it lies are the game's; the format has no extension of its own.

## Later

- Templates as an asset type of the editor, with a view of their clouds.
- Which drawn point matched a template's marked point (an arrow's tip), if `Up` turns out not to be
  enough.
