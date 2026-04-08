import argparse
import json
import numpy as np
import pandas as pd
from sklearn.neural_network import MLPRegressor
from sklearn.preprocessing import StandardScaler
import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--out", default="bc_policy.json")
    ap.add_argument("--plot", default="bc_loss.png")
    ap.add_argument("--hidden", default="128,128")
    ap.add_argument("--iters", type=int, default=200)
    args = ap.parse_args()

    df = pd.read_csv(args.csv)

    target_cols = ["targetThrottle", "targetBrake", "targetSteer"]
    drop_cols = target_cols + ["targetHandbrake", "dt", "carIndex", "isAI", "sourceTag"]

    X = df.drop(columns=[c for c in drop_cols if c in df.columns]).values.astype(np.float32)
    y = df[target_cols].values.astype(np.float32)

    scaler = StandardScaler()
    Xs = scaler.fit_transform(X)

    hidden = tuple(int(x) for x in args.hidden.split(",") if x.strip())
    model = MLPRegressor(
        hidden_layer_sizes=hidden,
        activation="relu",
        solver="adam",
        learning_rate_init=1e-3,
        max_iter=args.iters,
        random_state=42,
        verbose=True
    )
    model.fit(Xs, y)

    layers = []
    coefs = model.coefs_
    intercepts = model.intercepts_
    for i, (W, b) in enumerate(zip(coefs, intercepts)):
        activation = "relu"
        if i == len(coefs) - 1:
            activation = "linear"
        layers.append({
            "in": int(W.shape[0]),
            "out": int(W.shape[1]),
            "activation": activation,
            "weights": W.T.reshape(-1).tolist(),  # [out][in]
            "bias": b.tolist()
        })

    payload = {
        "input_mean": scaler.mean_.tolist(),
        "input_std": scaler.scale_.tolist(),
        "layers": layers
    }

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(payload, f)

    if hasattr(model, "loss_curve_"):
        plt.figure(figsize=(8,5))
        plt.plot(model.loss_curve_)
        plt.title("BC training loss")
        plt.xlabel("Iteration")
        plt.ylabel("Loss")
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(args.plot, dpi=150)

    summary = {
        "n_samples": int(len(df)),
        "n_features": int(X.shape[1]),
        "hidden": hidden,
        "iters": int(args.iters),
        "final_loss": float(model.loss_)
    }
    with open(args.out + ".summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print("Saved:", args.out)
    print("Summary:", summary)

if __name__ == "__main__":
    main()