import js.Browser;
import pixi.core.display.Container;
import pixi.core.display.DisplayObject;

import pixi.core.math.Matrix;
import pixi.core.display.Bounds;
import pixi.core.math.shapes.Rectangle;
import pixi.core.math.Point;

using DisplayObjectHelper;

class FlowRenderFrame extends FlowContainer {
	private var view : js.html.CanvasElement = cast(Browser.document.createElement('canvas'), js.html.CanvasElement);
	private var context : Dynamic;
	public var stageChanged : Bool = true;

	public function new(?worldVisible : Bool = false) {
		super();

		visible = worldVisible;
		clipVisible = worldVisible;
		interactiveChildren = false;

		view.style.zIndex = AccessWidget.zIndexValues.canvas;
		untyped view.style.pointerEvents = "none";
		context = view.getContext("2d", { alpha : true });
		stage = this;

		RenderSupportJSPixi.on("postdrawframe", animate);
		on("removed", function() {
			RenderSupportJSPixi.off("postdrawframe", animate);

			if (view.parentNode == Browser.document.body) {
				Browser.document.body.removeChild(view);
			}
		});
	}

	private function animate(timestamp : Float) {
		if (!this.visible || this.worldAlpha <= 0 || !this.renderable)
		{
			if (view.parentNode == Browser.document.body) {
				Browser.document.body.removeChild(view);
			}

			return;
		}

		if (stageChanged /*|| VideoClip.NeedsDrawing()*/) {
			stageChanged = false;

			var renderer : Dynamic = RenderSupportJSPixi.PixiRenderer;

			var previousView = renderer.view;
			var previousContext = renderer.context;
			var previousRootContext = untyped renderer.rootContext;
			var previousTransparent = renderer.transparent;

			view.width = previousView.width;
			view.height = previousView.height;

			renderer.view = view;
			renderer.context = context;
			untyped renderer.rootContext = context;
			renderer.transparent = true;

			for (child in children) {
				renderer.render(child, null, false, null, !transformChanged);
			}

			transformChanged = false;

			if (previousView.nextSibling != null) {
				Browser.document.body.insertBefore(view, previousView.nextSibling);
			} else {
				Browser.document.body.appendChild(view);
			}

			renderer.view = previousView;
			renderer.context = previousContext;
			untyped renderer.rootContext = previousRootContext;
			renderer.transparent = previousTransparent;
		}
	}

	public function renderCanvas(renderer : pixi.core.renderers.canvas.CanvasRenderer) {
		return;
	}
}