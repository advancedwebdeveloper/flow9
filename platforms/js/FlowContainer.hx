import pixi.core.display.Container;
import pixi.core.display.DisplayObject;

using DisplayObjectHelper;

class FlowContainer extends Container {
	public var scrollRect : FlowGraphics;
	private var _visible : Bool = true;
	private var clipVisible : Bool = false;
	public var transformChanged : Bool = true;
	private var childrenChanged : Bool = true;
	public var stage : Dynamic;

	public function new(?worldVisible : Bool = false) {
		super();

		visible = worldVisible;
		clipVisible = worldVisible;
		interactiveChildren = false;
	}

	public override function addChild<T:DisplayObject>(child : T) : T {
		if (stage != null) {
			untyped child.stage = stage;
		}

		var newChild = super.addChild(child);

		if (newChild != null) {
			newChild.updateClipInteractive(interactiveChildren);

			if (getClipVisible()) {
				newChild.updateClipWorldVisible();
				newChild.invalidateStage();
			}

			childrenChanged = true;
			emitEvent("childrenchanged");
		}

		return newChild;
	}

	public override function addChildAt<T:DisplayObject>(child : T, index : Int) : T {
		if (stage != null) {
			untyped child.stage = stage;
		}

		var newChild = super.addChildAt(child, index > children.length ? children.length : index);

		if (newChild != null) {
			newChild.updateClipInteractive(interactiveChildren);

			if (getClipVisible()) {
				newChild.updateClipWorldVisible();
				newChild.invalidateStage();
			}

			childrenChanged = true;
			emitEvent("childrenchanged");
		}

		return newChild;
	}

	public override function removeChild(child : DisplayObject) : DisplayObject {
		untyped child.stage = null;

		var oldChild = super.removeChild(child);

		if (oldChild != null) {
			updateClipInteractive();

			invalidateStage();

			childrenChanged = true;
			emitEvent("childrenchanged");
		}

		return oldChild;
	}
}